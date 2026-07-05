#include "nsc_qt/widgets/pipeline_events_widget.h"
#include "nsc_qt/instr_format.h"
#include "nsc_qt/ui_scale.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollBar>
#include <QVBoxLayout>

namespace nsc::qt {

namespace {

// Kind is stashed in each item's UserRole so setDarkMode() can re-colour
// the whole history when the theme flips.
constexpr int kKindRole = Qt::UserRole;

QString instr_or(const mips::StageSnapshot& s, const QString& fallback) {
    if (!s.valid || s.raw == 0) return fallback;
    return QString::fromStdString(format_instr(s.raw));
}

}  // anonymous namespace

PipelineEventsWidget::PipelineEventsWidget(QWidget* parent) : QWidget(parent) {
    auto* vl = new QVBoxLayout(this);
    vl->setContentsMargins(4, 4, 4, 4);
    vl->setSpacing(4);

    // Header row: entry count + clear button.
    auto* header = new QHBoxLayout;
    count_lbl_   = new QLabel(tr("0 events"), this);
    count_lbl_->setFont(scale::monoFont(scale::kFontSizeDense));
    header->addWidget(count_lbl_);
    header->addStretch();
    auto* clear_btn = new QPushButton(tr("Clear"), this);
    clear_btn->setToolTip(tr("Clear the event log (the simulation is not affected)"));
    connect(clear_btn, &QPushButton::clicked, this, &PipelineEventsWidget::clear);
    header->addWidget(clear_btn);
    vl->addLayout(header);

    list_ = new QListWidget(this);
    list_->setFont(scale::monoFont(scale::kFontSizeDense));
    list_->setSelectionMode(QAbstractItemView::NoSelection);
    list_->setFocusPolicy(Qt::NoFocus);
    list_->setUniformItemSizes(true);
    vl->addWidget(list_);
}

QColor PipelineEventsWidget::kindColor(Kind kind) const {
    // Matches the schematic's wire/hazard palette so the same event reads
    // in the same colour across both panels.
    switch (kind) {
    case Kind::FwdExMem:
        return dark_mode_ ? QColor("#FFA726") : QColor("#E65100");
    case Kind::FwdMemWb:
        return dark_mode_ ? QColor("#CE93D8") : QColor("#7B1FA2");
    case Kind::Stall:
        return dark_mode_ ? QColor("#FFB74D") : QColor("#B45309");
    case Kind::Flush:
        return dark_mode_ ? QColor("#FF5252") : QColor("#C62828");
    case Kind::Success:
        return dark_mode_ ? QColor("#81C784") : QColor("#2E7D32");
    case Kind::Error:
        return dark_mode_ ? QColor("#FF5252") : QColor("#C62828");
    case Kind::Info:
    default:
        return dark_mode_ ? QColor("#9CDCFE") : QColor("#00529B");
    }
}

void PipelineEventsWidget::logEvent(Kind kind, uint64_t cycle, const QString& text) {
    // Only auto-scroll when the user is already at the bottom, so reading
    // back through history isn't yanked away by new events.
    auto*      sb        = list_->verticalScrollBar();
    const bool at_bottom = sb->value() >= sb->maximum() - 2;

    auto* item = new QListWidgetItem(QStringLiteral("%1  ● %2").arg(cycle, 5).arg(text), list_);
    item->setForeground(kindColor(kind));
    item->setData(kKindRole, static_cast<int>(kind));

    while (list_->count() > kMaxEntries)
        delete list_->takeItem(0);

    count_lbl_->setText(tr("%n event(s)", nullptr, list_->count()));
    if (at_bottom) list_->scrollToBottom();
}

void PipelineEventsWidget::updateCycle(const mips::PipelineState& state) {
    const auto cycle = static_cast<quint64>(state.cycle);

    // Rising-edge suppression: an event repeated on the very next cycle
    // (multi-cycle stall, back-to-back forwards of the same pair) logs once.
    auto log_edge = [&](Kind kind, const QString& text) {
        const auto it        = recent_.constFind(text);
        const bool continued = it != recent_.constEnd() && (*it + 1 == cycle || *it == cycle);
        recent_[text]        = cycle;
        if (!continued) logEvent(kind, cycle, text);
    };

    const auto& ex  = state.stages[2];
    const auto& mem = state.stages[3];

    if (state.fwd_ex_to_ex_a)
        log_edge(Kind::FwdExMem, tr("EX/MEM→EX forward, ALU input A: %1").arg(instr_or(ex, "?")));
    if (state.fwd_ex_to_ex_b)
        log_edge(Kind::FwdExMem, tr("EX/MEM→EX forward, ALU input B: %1").arg(instr_or(ex, "?")));
    if (state.fwd_mem_to_ex_a)
        log_edge(Kind::FwdMemWb, tr("MEM/WB→EX forward, ALU input A: %1").arg(instr_or(ex, "?")));
    if (state.fwd_mem_to_ex_b)
        log_edge(Kind::FwdMemWb, tr("MEM/WB→EX forward, ALU input B: %1").arg(instr_or(ex, "?")));
    if (state.load_stall)
        log_edge(
            Kind::Stall,
            tr("load-use stall: %1 — bubble inserted").arg(instr_or(ex, QStringLiteral("lw"))));
    if (state.branch_flush)
        log_edge(Kind::Flush, tr("branch taken: %1 — younger instructions flushed")
                                  .arg(instr_or(mem, QStringLiteral("branch"))));
}

void PipelineEventsWidget::clear() {
    list_->clear();
    recent_.clear();
    count_lbl_->setText(tr("0 events"));
}

void PipelineEventsWidget::setDarkMode(bool dark) {
    dark_mode_ = dark;
    for (int i = 0; i < list_->count(); ++i) {
        auto* item = list_->item(i);
        item->setForeground(kindColor(static_cast<Kind>(item->data(kKindRole).toInt())));
    }
}

int PipelineEventsWidget::eventCount() const {
    return list_->count();
}

}  // namespace nsc::qt
