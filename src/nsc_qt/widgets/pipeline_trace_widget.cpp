#include "nsc_qt/widgets/pipeline_trace_widget.h"
#include "mips/decoder.h"
#include "nsc_qt/ui_scale.h"

#include <QHeaderView>
#include <QShowEvent>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <algorithm>
#include <string>

namespace nsc::qt {

namespace {

const std::array<QColor, 5> kStageColors = {
    QColor("#BBDEFB"),  // IF
    QColor("#B2EBF2"),  // ID
    QColor("#C8E6C9"),  // EX
    QColor("#FFF9C4"),  // MEM
    QColor("#FFCDD2"),  // WB
};
const std::array<QColor, 5> kStageColorsDark = {
    QColor("#0D47A1"), QColor("#006064"), QColor("#1B5E20"), QColor("#F57F17"), QColor("#B71C1C"),
};
constexpr std::array<const char*, 5> kStageNames = {"IF", "ID", "EX", "MEM", "WB"};

// Short mnemonic for a raw instruction word (assembly notation -- not
// routed through tr(), consistent with the datapath widget).
QString short_mnemonic(uint32_t raw) {
    if (raw == 0) return QStringLiteral("nop");
    const auto d = mips::Decoder::decode(raw);
    if (!d) return QStringLiteral("???");
    const std::string_view mn = mips::Decoder::mnemonic(*d);
    return QString::fromLatin1(mn.data(), static_cast<qsizetype>(mn.size()));
}

}  // anonymous namespace

int PipelineTraceWidget::InstrRow::lastStage() const noexcept {
    for (int s = 4; s >= 0; --s)
        if (cycle_in_stage[static_cast<std::size_t>(s)] != 0) return s;
    return -1;
}

uint64_t PipelineTraceWidget::InstrRow::lastCycle() const noexcept {
    const int s = lastStage();
    return s < 0 ? 0 : cycle_in_stage[static_cast<std::size_t>(s)];
}

PipelineTraceWidget::PipelineTraceWidget(QWidget* parent)
    : QWidget(parent), label_font_(scale::monoFont(scale::kFontSizeDense)),
      stage_font_(scale::monoFont(scale::kFontSizeDense, true)) {
    auto* vl = new QVBoxLayout(this);
    vl->setContentsMargins(4, 4, 4, 4);

    table_ = new QTableWidget(0, 1 + kMaxCycles, this);
    table_->setFont(label_font_);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setAlternatingRowColors(true);
    table_->setShowGrid(true);
    table_->verticalHeader()->setDefaultSectionSize(26);
    table_->horizontalHeader()->setDefaultSectionSize(46);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->setHorizontalHeaderItem(0, new QTableWidgetItem(tr("Instruction")));
    for (int c = 1; c <= kMaxCycles; ++c)
        table_->setHorizontalHeaderItem(c, new QTableWidgetItem);
    vl->addWidget(table_);
    rebuildTable();
}

void PipelineTraceWidget::clear() {
    rows_.clear();
    last_cycle_ = 0;
    rebuildTable();
}

void PipelineTraceWidget::setDarkMode(bool dark) {
    dark_mode_ = dark;
    rebuildTable();
}

void PipelineTraceWidget::updateCycle(const mips::PipelineState& state) {
    const uint64_t cycle = state.cycle;

    // A cycle that is not later than the last one (reset, reload) or that jumps
    // past the whole window (a throttled run reports one cycle in thousands)
    // cannot extend the diagram honestly, so start a new one.
    if (cycle <= last_cycle_ || cycle - last_cycle_ > kMaxCycles) rows_.clear();
    last_cycle_ = cycle;

    // Oldest stage first, so an older instance claims its row before a younger
    // instance of the same PC can. An instance in stage s continues the row of
    // the same PC that was last seen in stage s-1 in an earlier cycle; anything
    // else is a new instance (a fetch, or a pipeline this view has not seen).
    for (int s = 4; s >= 0; --s) {
        const auto& snap = state.stages[static_cast<std::size_t>(s)];
        if (!snap.valid || snap.stalled || snap.flushed) continue;

        InstrRow* row = nullptr;
        for (auto& r : rows_) {
            if (r.pc != snap.pc || r.lastStage() != s - 1 || r.lastCycle() >= cycle) continue;
            if (row == nullptr || r.lastCycle() > row->lastCycle()) row = &r;
        }
        if (row == nullptr) {
            rows_.push_back({snap.pc, snap.raw, {}});
            row = &rows_.back();
        }
        row->cycle_in_stage[static_cast<std::size_t>(s)] = cycle;
    }

    // Drop instances whose every stage has scrolled out of the window.
    const uint64_t first_visible = cycle > kMaxCycles ? cycle - kMaxCycles + 1 : 1;
    std::erase_if(rows_, [&](const InstrRow& r) { return r.lastCycle() < first_visible; });

    if (isVisible())
        rebuildTable();
    else
        dirty_ = true;
}

void PipelineTraceWidget::showEvent(QShowEvent* ev) {
    QWidget::showEvent(ev);
    if (dirty_) rebuildTable();
}

void PipelineTraceWidget::rebuildTable() {
    dirty_ = false;

    // Items and header labels are created once and updated in place: tearing
    // the table down every cycle cost hundreds of allocations per step.
    const uint64_t base = last_cycle_ > kMaxCycles ? last_cycle_ - kMaxCycles : 0;
    for (int c = 0; c < kMaxCycles; ++c)
        table_->horizontalHeaderItem(1 + c)->setText(
            QString::number(base + static_cast<uint64_t>(c) + 1));

    const int n_rows = static_cast<int>(rows_.size());
    if (table_->rowCount() != n_rows) table_->setRowCount(n_rows);

    const auto& palette = dark_mode_ ? kStageColorsDark : kStageColors;
    for (int ri = 0; ri < n_rows; ++ri) {
        const auto& r = rows_[static_cast<std::size_t>(ri)];

        auto* lbl = table_->item(ri, 0);
        if (lbl == nullptr) {
            lbl = new QTableWidgetItem;
            lbl->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            lbl->setFont(label_font_);
            table_->setItem(ri, 0, lbl);
        }
        lbl->setText(
            QStringLiteral("0x%1  %2").arg(r.pc, 4, 16, QChar('0')).arg(short_mnemonic(r.raw)));

        for (int ci = 0; ci < kMaxCycles; ++ci) {
            const uint64_t col_cycle = base + static_cast<uint64_t>(ci) + 1;
            int            stage     = -1;
            for (int s = 0; s < 5 && stage < 0; ++s)
                if (r.cycle_in_stage[static_cast<std::size_t>(s)] == col_cycle) stage = s;

            auto* item = table_->item(ri, 1 + ci);
            if (item == nullptr) {
                item = new QTableWidgetItem;
                item->setTextAlignment(Qt::AlignCenter);
                item->setFont(stage_font_);
                table_->setItem(ri, 1 + ci, item);
            }
            if (stage < 0) {
                item->setText(QString());
                item->setData(Qt::BackgroundRole, QVariant());
                item->setData(Qt::ForegroundRole, QVariant());
            } else {
                item->setText(QString::fromLatin1(kStageNames[static_cast<std::size_t>(stage)]));
                item->setBackground(palette[static_cast<std::size_t>(stage)]);
                item->setForeground(dark_mode_ ? Qt::white : Qt::black);
            }
        }
    }
}

}  // namespace nsc::qt
