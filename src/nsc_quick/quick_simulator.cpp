#include "nsc_quick/quick_simulator.h"

#include "nsc_qt/assembler.h"
#include "nsc_qt/examples.h"
#include "mips/decoder.h"
#include "mips/disassembler.h"
#include "mips/pipelined_cpu.h"
#include "mips/single_cycle_cpu.h"

#include <QChar>

namespace nsc::quick {

namespace {

constexpr std::size_t kMemorySize = 1u << 20;  // 1 MiB, same as the widget GUI

QString hex32(uint32_t v) {
    return QStringLiteral("0x%1").arg(v, 8, 16, QLatin1Char('0'));
}

QString disasm(uint32_t raw, uint32_t pc) {
    if (const auto dec = mips::Decoder::decode(raw))
        return QString::fromStdString(mips::Disassembler::to_string(*dec, pc));
    return QStringLiteral(".word %1").arg(hex32(raw));
}

std::unique_ptr<mips::IProcessor> makeProcessor(const QString& model) {
    if (model == QLatin1String("single"))
        return std::make_unique<mips::SingleCycleCpu>(kMemorySize);
    return std::make_unique<mips::PipelinedCpu>(kMemorySize);
}

} // namespace

// ─── RegisterModel ────────────────────────────────────────────────────────────

RegisterModel::RegisterModel(QObject* parent) : QAbstractListModel(parent) {}

int RegisterModel::rowCount(const QModelIndex&) const {
    return static_cast<int>(values_.size());
}

QVariant RegisterModel::data(const QModelIndex& idx, int role) const {
    if (!idx.isValid() || idx.row() < 0 || idx.row() >= 32) return {};
    const auto i = static_cast<uint8_t>(idx.row());
    switch (role) {
    case NumberRole:  return idx.row();
    case NameRole:    return QStringLiteral("$%1").arg(
                          QString::fromUtf8(mips::register_abi_name(i).data(),
                              static_cast<int>(mips::register_abi_name(i).size())));
    case HexRole:     return hex32(values_[i]);
    case DecRole:     return QString::number(static_cast<int32_t>(values_[i]));
    case ChangedRole: return idx.row() == last_written_;
    default:          return {};
    }
}

QHash<int, QByteArray> RegisterModel::roleNames() const {
    return {
        { NumberRole,  "number"  },
        { NameRole,    "name"    },
        { HexRole,     "hex"     },
        { DecRole,     "dec"     },
        { ChangedRole, "changed" },
    };
}

void RegisterModel::refresh(const mips::RegisterFile& regs) {
    const auto& raw     = regs.raw();
    const int   written = regs.last_written();

    for (int i = 0; i < 32; ++i) {
        const bool value_changed = values_[static_cast<size_t>(i)] != raw[static_cast<size_t>(i)];
        const bool mark_changed  = (i == written) != (i == last_written_);
        values_[static_cast<size_t>(i)] = raw[static_cast<size_t>(i)];
        if (value_changed || mark_changed) {
            const QModelIndex mi = index(i);
            emit dataChanged(mi, mi, { HexRole, DecRole, ChangedRole });
        }
    }
    last_written_ = written;
}

void RegisterModel::resetAll() {
    beginResetModel();
    values_.fill(0);
    last_written_ = -1;
    endResetModel();
}

// ─── QuickSimulator ───────────────────────────────────────────────────────────

QuickSimulator::QuickSimulator(QObject* parent) : QObject(parent) {
    rebuildController();
}

void QuickSimulator::rebuildController() {
    ctl_ = std::make_unique<nsc::qt::SimulatorController>(makeProcessor(model_));
    ctl_->setExecutionSpeed(speed_);
    connectController();

    stats_ = {};
    trace_.clear();
    stages_.clear();
    hazards_.clear();
    reg_model_.resetAll();

    if (!last_program_.empty()) {
        program_loaded_ = ctl_->loadProgram(last_program_);
        emit programLoadedChanged();
    }

    emit statsChanged();
    emit traceChanged();
    emit pipelineChanged();
    setStatus(QStringLiteral("Idle"));
}

void QuickSimulator::connectController() {
    connect(ctl_.get(), &nsc::qt::SimulatorController::pipelineStateChanged,
            this, [this](const mips::PipelineState& ps) { onPipeline(ps); });

    connect(ctl_.get(), &nsc::qt::SimulatorController::statisticsUpdated,
            this, [this](const nsc::qt::SimulatorStatistics& s) {
                stats_ = s;
                emit statsChanged();
            });

    connect(ctl_.get(), &nsc::qt::SimulatorController::halted, this, [this] {
        setStatus(QStringLiteral("Halted"));
        emit runningChanged();
    });

    connect(ctl_.get(), &nsc::qt::SimulatorController::faulted, this, [this] {
        setStatus(QStringLiteral("Faulted"));
        emit runningChanged();
    });

    connect(ctl_.get(), &nsc::qt::SimulatorController::breakpointHit,
            this, [this](uint32_t pc) {
                setStatus(QStringLiteral("Breakpoint @ %1").arg(hex32(pc)));
                emit runningChanged();
            });
}

// ── Transport ─────────────────────────────────────────────────────────────────

void QuickSimulator::step() {
    if (!program_loaded_) return;
    ctl_->stepCycle();
    if (status_ == QLatin1String("Idle") || status_.startsWith(QLatin1String("Breakpoint")))
        setStatus(QStringLiteral("Paused"));
}

void QuickSimulator::runPause() {
    if (!program_loaded_) return;
    if (ctl_->isRunning()) {
        ctl_->stop();
        setStatus(QStringLiteral("Paused"));
    } else {
        ctl_->run();
        setStatus(QStringLiteral("Running"));
    }
    emit runningChanged();
}

void QuickSimulator::reset() {
    ctl_->stop();
    ctl_->reset();
    stats_ = {};
    trace_.clear();
    stages_.clear();
    hazards_.clear();
    reg_model_.resetAll();

    if (!last_program_.empty())
        program_loaded_ = ctl_->loadProgram(last_program_);

    emit statsChanged();
    emit traceChanged();
    emit pipelineChanged();
    emit runningChanged();
    setStatus(QStringLiteral("Idle"));
}

QString QuickSimulator::assembleAndLoad(const QString& source) {
    const auto result = nsc::qt::assemble(source.toStdString());
    if (!result.ok()) {
        const QString msg = QString::fromStdString(*result.error);
        emit assemblyError(msg);
        return msg;
    }

    ctl_->stop();
    ctl_->reset();
    last_program_ = result.words;
    program_loaded_ = ctl_->loadProgram(last_program_);

    stats_ = {};
    trace_.clear();
    reg_model_.resetAll();

    emit programLoadedChanged();
    emit statsChanged();
    emit traceChanged();
    emit runningChanged();
    setStatus(QStringLiteral("Loaded %1 instructions").arg(last_program_.size()));
    return {};
}

// ── Examples ──────────────────────────────────────────────────────────────────

QStringList QuickSimulator::exampleNames() const {
    QStringList names;
    for (const auto& ex : nsc::qt::exampleProgramCatalog())
        names << ex.name;
    return names;
}

QString QuickSimulator::exampleSource(int index) const {
    const auto& cat = nsc::qt::exampleProgramCatalog();
    if (index < 0 || index >= static_cast<int>(cat.size())) return {};
    return cat[static_cast<size_t>(index)].source;
}

// ── Memory ────────────────────────────────────────────────────────────────────

QStringList QuickSimulator::memoryRows(quint32 base, int rows) const {
    QStringList out;
    out.reserve(rows);
    const auto& mem = ctl_->memory();
    quint32 addr = base & ~0xFu;

    for (int r = 0; r < rows; ++r, addr += 16) {
        QString hex, ascii;
        for (int b = 0; b < 16; ++b) {
            const auto byte = mem.read_byte(addr + static_cast<quint32>(b));
            if (byte) {
                hex += QStringLiteral("%1 ").arg(*byte, 2, 16, QLatin1Char('0'));
                const QChar c(*byte);
                ascii += (c.isPrint() && *byte < 0x7F) ? c : QLatin1Char('.');
            } else {
                hex += QStringLiteral("-- ");
                ascii += QLatin1Char(' ');
            }
            if (b == 7) hex += QLatin1Char(' ');
        }
        out << QStringLiteral("%1  %2 |%3|")
                   .arg(addr, 8, 16, QLatin1Char('0')).arg(hex, ascii);
    }
    return out;
}

// ── Breakpoints ───────────────────────────────────────────────────────────────

void QuickSimulator::toggleBreakpoint(quint32 pc) {
    if (ctl_->hasBreakpoint(pc)) ctl_->clearBreakpoint(pc);
    else                         ctl_->setBreakpoint(pc);
    emit breakpointsChanged();
}

bool QuickSimulator::hasBreakpoint(quint32 pc) const {
    return ctl_->hasBreakpoint(pc);
}

// ── Setters ───────────────────────────────────────────────────────────────────

bool QuickSimulator::isRunning() const { return ctl_ && ctl_->isRunning(); }

void QuickSimulator::setSpeed(int s) {
    s = std::clamp(s, 0, 100);
    if (s == speed_) return;
    speed_ = s;
    ctl_->setExecutionSpeed(s);
    emit speedChanged();
}

void QuickSimulator::setModel(const QString& m) {
    if (m == model_ || (m != QLatin1String("pipelined") && m != QLatin1String("single")))
        return;
    model_ = m;
    rebuildController();
    emit modelChanged();
}

// ── Pipeline snapshot → QML ───────────────────────────────────────────────────

void QuickSimulator::onPipeline(const mips::PipelineState& ps) {
    stages_.clear();
    for (const auto& st : ps.stages) {
        QVariantMap m;
        m.insert(QStringLiteral("name"),    QString::fromLatin1(st.name));
        m.insert(QStringLiteral("valid"),   st.valid);
        m.insert(QStringLiteral("stalled"), st.stalled);
        m.insert(QStringLiteral("flushed"), st.flushed);
        m.insert(QStringLiteral("pc"),      st.valid ? hex32(st.pc) : QString());
        m.insert(QStringLiteral("text"),
                 st.valid ? disasm(st.raw, st.pc)
                 : st.stalled ? QStringLiteral("bubble (stall)")
                 : st.flushed ? QStringLiteral("bubble (flush)")
                 : QStringLiteral("—"));
        stages_.append(m);
    }

    hazards_ = {
        { QStringLiteral("fwdExA"),      ps.fwd_ex_to_ex_a  },
        { QStringLiteral("fwdExB"),      ps.fwd_ex_to_ex_b  },
        { QStringLiteral("fwdMemA"),     ps.fwd_mem_to_ex_a },
        { QStringLiteral("fwdMemB"),     ps.fwd_mem_to_ex_b },
        { QStringLiteral("loadStall"),   ps.load_stall      },
        { QStringLiteral("branchFlush"), ps.branch_flush    },
    };

    reg_model_.refresh(ctl_->registers());
    appendTrace(ps);
    emit pipelineChanged();
}

// ── Trace grid ────────────────────────────────────────────────────────────────

void QuickSimulator::appendTrace(const mips::PipelineState& ps) {
    const quint64 cycle = ps.cycle;
    if (cycle > kTraceMaxCycles) return;  // bounded; Save Trace covers long runs

    // A valid IF stage means an instruction entered the pipeline → new row.
    const auto& if_stage = ps.stages[0];
    if (if_stage.valid) {
        TraceRow row;
        row.pc          = if_stage.pc;
        row.start_cycle = cycle;
        row.label       = QStringLiteral("%1  %2")
                              .arg(hex32(if_stage.pc), disasm(if_stage.raw, if_stage.pc));
        trace_.push_back(std::move(row));
        if (trace_.size() > kTraceMaxRows)
            trace_.erase(trace_.begin());
    }

    // Mark the stage each live row occupies this cycle.
    for (auto& row : trace_) {
        QString tag = QStringLiteral("  ");
        for (size_t s = 0; s < ps.stages.size(); ++s) {
            const auto& st = ps.stages[s];
            if (st.valid && st.pc == row.pc &&
                cycle >= row.start_cycle && cycle - row.start_cycle < 16) {
                // Nearest plausible stage for this row's instruction instance:
                // pick the earliest stage whose distance matches the row age.
                if (static_cast<quint64>(s) == cycle - row.start_cycle ||
                    tag == QLatin1String("  ")) {
                    tag = QString::fromLatin1(st.name);
                }
            }
        }
        if (ps.load_stall && tag == QLatin1String("  ") &&
            cycle >= row.start_cycle && cycle - row.start_cycle < 8)
            tag = QStringLiteral("**");
        row.cells << tag;
    }
    emit traceChanged();
}

QVariantList QuickSimulator::traceRows() const {
    QVariantList out;
    for (const auto& row : trace_) {
        QVariantMap m;
        m.insert(QStringLiteral("label"), row.label);
        m.insert(QStringLiteral("start"), static_cast<qulonglong>(row.start_cycle));
        m.insert(QStringLiteral("cells"), row.cells);
        out.append(m);
    }
    return out;
}

// ── Status ────────────────────────────────────────────────────────────────────

void QuickSimulator::setStatus(const QString& s) {
    if (s == status_) return;
    status_ = s;
    emit statusChanged();
}

} // namespace nsc::quick
