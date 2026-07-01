#include "nsc_qt/widgets/pipeline_trace_widget.h"
#include "mips/decoder.h"
#include "mips/registers.h"

#include <QHeaderView>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <sstream>

namespace nsc::qt {

namespace {

static const QColor STAGE_CELL_COLORS[5] = {
    QColor("#BBDEFB"),  // IF
    QColor("#B2EBF2"),  // ID
    QColor("#C8E6C9"),  // EX
    QColor("#FFF9C4"),  // MEM
    QColor("#FFCDD2"),  // WB
};
static const QColor STAGE_CELL_COLORS_DARK[5] = {
    QColor("#0D47A1"),
    QColor("#006064"),
    QColor("#1B5E20"),
    QColor("#F57F17"),
    QColor("#B71C1C"),
};

static int stage_index(const char* name) {
    if (name[0] == 'I' && name[1] == 'F') return 0;
    if (name[0] == 'I' && name[1] == 'D') return 1;
    if (name[0] == 'E' && name[1] == 'X') return 2;
    if (name[0] == 'M')                   return 3;
    if (name[0] == 'W')                   return 4;
    return -1;
}

// Short mnemonic for a raw instruction word.
static std::string short_mnemonic(uint32_t raw) {
    if (raw == 0) return "nop";
    auto d = mips::Decoder::decode(raw);
    if (!d) return "???";
    return std::string(mips::Decoder::mnemonic(*d));
}

} // anonymous namespace

PipelineTraceWidget::PipelineTraceWidget(QWidget* parent) : QWidget(parent)
{
    auto* vl = new QVBoxLayout(this);
    vl->setContentsMargins(4, 4, 4, 4);

    table_ = new QTableWidget(this);
    table_->setFont(QFont("monospace", 9));
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setAlternatingRowColors(true);
    table_->setShowGrid(true);
    table_->verticalHeader()->setDefaultSectionSize(24);
    table_->horizontalHeader()->setDefaultSectionSize(44);
    vl->addWidget(table_);
}

void PipelineTraceWidget::clear()
{
    rows_.clear();
    current_cycle_ = 0;
    cycle_base_    = 0;
    rebuildTable();
}

void PipelineTraceWidget::setDarkMode(bool dark)
{
    dark_mode_ = dark;
    rebuildTable();
}

void PipelineTraceWidget::updateCycle(const mips::PipelineState& state)
{
    ++current_cycle_;

    // Ensure we have MAX_CYCLES visible columns.
    if (current_cycle_ > MAX_CYCLES)
        cycle_base_ = current_cycle_ - MAX_CYCLES;

    // Walk each active stage and record it.
    for (std::size_t si = 0; si < 5; ++si) {
        const auto& snap = state.stages[si];
        if (!snap.valid || snap.stalled || snap.flushed) continue;

        // Find or create row for this instruction (keyed by PC).
        InstrRow* row_ptr = nullptr;
        for (auto& r : rows_)
            if (r.pc == snap.pc) { row_ptr = &r; break; }

        if (!row_ptr) {
            rows_.push_back({snap.pc, snap.raw, {}});
            row_ptr = &rows_.back();
        }

        // Extend stage list to reach current cycle slot.
        const std::size_t col = static_cast<std::size_t>(current_cycle_ - 1 - cycle_base_);
        while (row_ptr->stages.size() <= col)
            row_ptr->stages.push_back("");
        row_ptr->stages[col] = snap.name;
    }

    // Keep only instructions visible within MAX_CYCLES window.
    const uint64_t prune_before = (current_cycle_ > MAX_CYCLES * 2)
                                  ? (current_cycle_ - MAX_CYCLES * 2) : 0;
    (void)prune_before; // keep all rows for now — table rebuild will clip columns

    rebuildTable();
}

void PipelineTraceWidget::rebuildTable()
{
    const int n_cols = MAX_CYCLES;
    const int n_rows = static_cast<int>(rows_.size());

    table_->clearContents();
    table_->setRowCount(n_rows);
    table_->setColumnCount(1 + n_cols);

    // Headers
    QStringList col_headers;
    col_headers << "Instruction";
    for (int c = 0; c < n_cols; ++c)
        col_headers << QString::number(static_cast<qulonglong>(cycle_base_ + static_cast<uint64_t>(c) + 1));
    table_->setHorizontalHeaderLabels(col_headers);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);

    for (int ri = 0; ri < n_rows; ++ri) {
        const auto& r = rows_[static_cast<std::size_t>(ri)];

        // Instruction label column
        const std::string mn = short_mnemonic(r.raw);
        auto* lbl_item = new QTableWidgetItem(
            QString("0x%1  %2").arg(r.pc, 4, 16, QChar('0'))
                               .arg(QString::fromStdString(mn)));
        lbl_item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        lbl_item->setFont(QFont("monospace", 9));
        table_->setItem(ri, 0, lbl_item);

        // Stage columns
        for (int ci = 0; ci < n_cols; ++ci) {
            const std::size_t col = static_cast<std::size_t>(ci);
            std::string stage_name;
            if (col < r.stages.size()) stage_name = r.stages[col];

            auto* item = new QTableWidgetItem(QString::fromStdString(stage_name));
            item->setTextAlignment(Qt::AlignCenter);

            if (!stage_name.empty()) {
                const int sidx = stage_index(stage_name.c_str());
                if (sidx >= 0) {
                    item->setBackground(dark_mode_ ? STAGE_CELL_COLORS_DARK[sidx]
                                                   : STAGE_CELL_COLORS[sidx]);
                    item->setForeground(dark_mode_ ? Qt::white : Qt::black);
                    QFont cell_f("monospace", 8, QFont::Bold);
                    item->setFont(cell_f);
                }
            }
            table_->setItem(ri, 1 + ci, item);
        }
    }
}

} // namespace nsc::qt
