#include "nsc_qt/widgets/register_widget.h"
#include "mips/decoder.h"
#include "mips/pipeline_units.h"
#include "mips/registers.h"
#include "nsc_qt/ui_scale.h"

#include <QDateTime>
#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace nsc::qt {

namespace {

QColor lerp_color(QColor a, QColor b, float t) {
    const auto mix = [t](int from, int to) {
        return static_cast<int>(static_cast<float>(from) + static_cast<float>(to - from) * t);
    };
    return {mix(a.red(), b.red()), mix(a.green(), b.green()), mix(a.blue(), b.blue())};
}

}  // anonymous namespace

// ── RegisterWidget::Cell ─────────────────────────────────────────────────────

RegisterWidget::Cell::Cell(QWidget* parent) : QWidget(parent) {
    auto* fl = new QVBoxLayout(this);
    fl->setContentsMargins(5, 2, 5, 2);
    fl->setSpacing(1);

    name_  = new QLabel(this);
    value_ = new QLabel(this);
    name_->setFont(scale::monoFont(scale::kFontSizeDense));
    value_->setFont(scale::monoFont(scale::kFontSizeBody));
    value_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    fl->addWidget(name_);
    fl->addWidget(value_);
}

void RegisterWidget::Cell::setNameText(const QString& text, const QColor& color) {
    name_->setText(text);
    QPalette pal = name_->palette();
    pal.setColor(QPalette::WindowText, color);
    name_->setPalette(pal);
}

void RegisterWidget::Cell::setValueText(const QString& text, const QColor& color) {
    value_->setText(text);
    QPalette pal = value_->palette();
    pal.setColor(QPalette::WindowText, color);
    value_->setPalette(pal);
}

void RegisterWidget::Cell::setBackground(const QColor& bg, const QColor& border) {
    if (bg_ == bg && border_ == border) return;  // unchanged -- skip the repaint
    bg_     = bg;
    border_ = border;
    update();
}

void RegisterWidget::Cell::paintEvent(QPaintEvent* /*ev*/) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(bg_);
    p.setPen(QPen(border_, 1));
    p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 3, 3);
}

// ── RegisterWidget ────────────────────────────────────────────────────────────

RegisterWidget::RegisterWidget(QWidget* parent) : QWidget(parent) {
    buildGrid();

    fade_timer_ = new QTimer(this);
    fade_timer_->setInterval(33);  // ~30fps while at least one cell is fading
    connect(fade_timer_, &QTimer::timeout, this, [this] {
        const qint64 now        = QDateTime::currentMSecsSinceEpoch();
        bool         any_fading = false;
        for (int i = 1; i < 32; ++i) {
            auto& cs = cells_[i];
            if (cs.fade_start_ms < 0) continue;
            if (now - cs.fade_start_ms >= kFadeDurationMs) {
                cs.fade_start_ms = -1;
            } else {
                any_fading = true;
            }
            updateCell(i);
        }
        if (!any_fading) fade_timer_->stop();
    });
}

void RegisterWidget::buildGrid() {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // Header bar
    auto* header = new QWidget(this);
    header->setFixedHeight(30);
    header->setObjectName("regHeader");
    auto* hl = new QHBoxLayout(header);
    hl->setContentsMargins(10, 0, 10, 0);
    auto* title = new QLabel(tr("Registers"), header);
    title->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    title->setFont(scale::monoFont(scale::kFontSizeHeader, true));
    hl->addWidget(title);
    hl->addStretch();
    outer->addWidget(header, 0);

    // Grid fills the remaining space
    auto* grid_container = new QWidget(this);
    auto* cl             = new QVBoxLayout(grid_container);
    cl->setContentsMargins(4, 4, 4, 4);
    cl->setSpacing(0);

    grid_ = new QGridLayout;
    grid_->setSpacing(3);
    cl->addLayout(grid_);

    // 4 columns × 8 rows = 32 registers
    for (int i = 0; i < 32; ++i) {
        const int col = i / 8;
        const int row = i % 8;

        auto* cell = new Cell(grid_container);
        cell->setMinimumHeight(36);
        cells_[i].widget = cell;

        grid_->addWidget(cell, row, col);
        updateCell(i);
    }

    // Uniform column and row stretch so the grid fills available space
    for (int c = 0; c < 4; ++c)
        grid_->setColumnStretch(c, 1);
    for (int r = 0; r < 8; ++r)
        grid_->setRowStretch(r, 1);

    outer->addWidget(grid_container, 1);
}

void RegisterWidget::updateCell(int idx) {
    auto&      cs = cells_[idx];
    const auto u  = static_cast<uint8_t>(idx);

    // Name text (register mnemonics like "$8 (t0)" are notation, not prose --
    // not routed through tr(), consistent with datapath_widget's mnemonics).
    QString name_str = QString("$%1").arg(idx);
    if (show_aliases_)
        name_str +=
            QString(" (%1)").arg(QString::fromStdString(std::string(mips::register_abi_name(u))));

    // Bumped from the previous 0x88 gray -- that was close to the WCAG AA
    // 4.5:1 threshold against the 0x2A dark background (audit Opportunity #8).
    const QColor text_dim  = dark_mode_ ? QColor(0x9E, 0x9E, 0x9E) : QColor(0x77, 0x77, 0x77);
    const QColor text_norm = dark_mode_ ? QColor(0xCC, 0xCC, 0xCC) : QColor(0x22, 0x22, 0x22);
    const QColor border    = dark_mode_ ? QColor(0x3C, 0x3C, 0x3C) : QColor(0xDD, 0xDD, 0xDD);

    QColor bg, name_color, val_color;
    if (idx == 0) {
        bg         = dark_mode_ ? QColor(0x28, 0x28, 0x28) : QColor(0xE8, 0xE8, 0xE8);
        name_color = text_dim;
        val_color  = text_dim;
    } else if (cs.fade_start_ms >= 0) {
        const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - cs.fade_start_ms;
        const float  t = std::clamp(static_cast<float>(elapsed) / kFadeDurationMs, 0.0f, 1.0f);
        const QColor green(0x66, 0xBB, 0x66);
        const QColor normal = dark_mode_ ? QColor(0x2A, 0x2A, 0x2A) : Qt::white;
        bg                  = lerp_color(green, normal, t);
        name_color          = dark_mode_ ? QColor(0xAA, 0xFF, 0xAA) : QColor(0x00, 0x55, 0x00);
        val_color           = dark_mode_ ? Qt::white : QColor(0x00, 0x44, 0x00);
    } else if (u == read_rs_ || u == read_rt_) {
        bg         = dark_mode_ ? QColor(0x00, 0x4D, 0x6B) : QColor(0xCC, 0xEA, 0xF5);
        name_color = dark_mode_ ? QColor(0x9C, 0xDC, 0xFE) : QColor(0x00, 0x52, 0x9B);
        val_color  = text_norm;
    } else {
        bg         = dark_mode_ ? QColor(0x2A, 0x2A, 0x2A) : Qt::white;
        name_color = text_dim;
        val_color  = text_norm;
    }

    cs.widget->setNameText(name_str, name_color);
    cs.widget->setValueText(QString("0x%1").arg(values_[idx], 8, 16, QChar('0')), val_color);
    cs.widget->setBackground(bg, border);
}

void RegisterWidget::startFade(int idx) {
    cells_[idx].fade_start_ms = QDateTime::currentMSecsSinceEpoch();
    if (!fade_timer_->isActive()) fade_timer_->start();
}

// Single per-cycle update. The read and write rules are the pipeline's own
// (mips::source_registers / destination_register), so a shift does not light
// rs, and MFC0 lights rt instead of the CP0 register number in rd.
void RegisterWidget::updateCycle(const mips::PipelineState&      state,
                                 const std::array<uint32_t, 32>& vals) {
    values_ = vals;

    // Registers read by the instruction in ID. Kept current while hidden, so
    // the highlight is correct the moment the panel is shown.
    read_rs_       = 0xFF;
    read_rt_       = 0xFF;
    const auto& id = state.stages[1];
    if (id.valid && id.raw != 0) {
        if (const auto d = mips::Decoder::decode(id.raw)) {
            const mips::SourceRegisters src = mips::source_registers(*d);
            if (src.a >= 0) read_rs_ = static_cast<uint8_t>(src.a);
            if (src.b >= 0) read_rt_ = static_cast<uint8_t>(src.b);
        }
    }

    // A closed panel or a background tab skips the fade and the 32 cell
    // refreshes; showEvent() refreshes it once.
    if (!isVisible()) {
        dirty_ = true;
        return;
    }

    const auto& wb = state.stages[4];
    if (wb.valid && !wb.stalled && !wb.flushed && wb.raw != 0) {
        if (const auto d = mips::Decoder::decode(wb.raw)) {
            const int dest = mips::destination_register(*d);
            if (dest > 0) startFade(dest);  // $zero never changes
        }
    }

    dirty_ = false;
    for (int i = 0; i < 32; ++i)
        updateCell(i);
}

void RegisterWidget::showEvent(QShowEvent* ev) {
    QWidget::showEvent(ev);
    if (!dirty_) return;
    dirty_ = false;
    for (int i = 0; i < 32; ++i)
        updateCell(i);
}

void RegisterWidget::setShowAliases(bool show) {
    show_aliases_ = show;
    for (int i = 0; i < 32; ++i)
        updateCell(i);
}

void RegisterWidget::setDarkMode(bool dark) {
    dark_mode_ = dark;
    for (int i = 0; i < 32; ++i)
        updateCell(i);
}

void RegisterWidget::clear() {
    values_.fill(0);
    read_rs_ = 0xFF;
    read_rt_ = 0xFF;
    for (auto& c : cells_)
        c.fade_start_ms = -1;
    fade_timer_->stop();
    for (int i = 0; i < 32; ++i)
        updateCell(i);
}

}  // namespace nsc::qt