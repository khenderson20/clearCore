#include "nsc_qt/widgets/register_widget.h"
#include "mips/decoder.h"
#include "mips/registers.h"

#include <QGridLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <algorithm>

namespace nsc::qt {

namespace {

static QColor lerp_color(QColor a, QColor b, float t) {
    return QColor(
        static_cast<int>(a.red()   + (b.red()   - a.red())   * t),
        static_cast<int>(a.green() + (b.green() - a.green()) * t),
        static_cast<int>(a.blue()  + (b.blue()  - a.blue())  * t)
    );
}

} // anonymous namespace

RegisterWidget::RegisterWidget(QWidget* parent) : QWidget(parent)
{
    buildGrid();
}

void RegisterWidget::buildGrid()
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // Header bar
    auto* header = new QWidget(this);
    header->setFixedHeight(28);
    header->setObjectName("regHeader");
    auto* hl = new QHBoxLayout(header);
    hl->setContentsMargins(10, 0, 10, 0);
    auto* title = new QLabel("Registers", header);
    title->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    QFont tf("monospace", 9, QFont::Bold);
    title->setFont(tf);
    hl->addWidget(title);
    hl->addStretch();
    outer->addWidget(header, 0);

    // Grid fills the remaining space
    auto* grid_container = new QWidget(this);
    auto* cl = new QVBoxLayout(grid_container);
    cl->setContentsMargins(4, 4, 4, 4);
    cl->setSpacing(0);

    grid_ = new QGridLayout;
    grid_->setSpacing(3);
    cl->addLayout(grid_);

    // 4 columns × 8 rows = 32 registers
    for (int i = 0; i < 32; ++i) {
        const int col = i / 8;
        const int row = i % 8;

        auto* frame = new QWidget(grid_container);
        frame->setMinimumHeight(34);
        auto* fl = new QVBoxLayout(frame);
        fl->setContentsMargins(5, 2, 5, 2);
        fl->setSpacing(1);

        cells_[i].name  = new QLabel(frame);
        cells_[i].value = new QLabel(frame);

        cells_[i].name->setFont(QFont("monospace", 8));
        cells_[i].value->setFont(QFont("monospace", 9));
        cells_[i].value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        fl->addWidget(cells_[i].name);
        fl->addWidget(cells_[i].value);

        grid_->addWidget(frame, row, col);
        updateCell(i);
    }

    // Uniform column and row stretch so the grid fills available space
    for (int c = 0; c < 4; ++c) grid_->setColumnStretch(c, 1);
    for (int r = 0; r < 8; ++r) grid_->setRowStretch(r, 1);

    outer->addWidget(grid_container, 1);
}

void RegisterWidget::updateCell(int idx)
{
    const auto& c  = cells_[idx];
    const uint8_t u = static_cast<uint8_t>(idx);

    // Name label
    QString name_str = QString("$%1").arg(idx);
    if (show_aliases_ && idx < 32)
        name_str += QString(" (%1)").arg(QString::fromStdString(
            std::string(mips::register_abi_name(u))));
    c.name->setText(name_str);

    // Value label
    c.value->setText(QString("0x%1").arg(values_[idx], 8, 16, QChar('0')));

    // Background and text colours
    QColor bg, name_color, val_color;
    const QColor text_dim  = dark_mode_ ? QColor(0x88,0x88,0x88) : QColor(0x77,0x77,0x77);
    const QColor text_norm = dark_mode_ ? QColor(0xCC,0xCC,0xCC) : QColor(0x22,0x22,0x22);

    if (idx == 0) {
        bg         = dark_mode_ ? QColor(0x28,0x28,0x28) : QColor(0xE8,0xE8,0xE8);
        name_color = text_dim;
        val_color  = text_dim;
    } else if (cells_[idx].fade > 0) {
        const float t = 1.0f - static_cast<float>(cells_[idx].fade) / 5.0f;
        const QColor green(0x66, 0xBB, 0x66);
        const QColor normal = dark_mode_ ? QColor(0x2A,0x2A,0x2A) : Qt::white;
        bg         = lerp_color(green, normal, t);
        name_color = dark_mode_ ? QColor(0xAA,0xFF,0xAA) : QColor(0x00,0x55,0x00);
        val_color  = dark_mode_ ? Qt::white : QColor(0x00,0x44,0x00);
    } else if (static_cast<uint8_t>(idx) == read_rs_ || static_cast<uint8_t>(idx) == read_rt_) {
        bg         = dark_mode_ ? QColor(0x00,0x4D,0x6B) : QColor(0xCC,0xEA,0xF5);
        name_color = dark_mode_ ? QColor(0x9C,0xDC,0xFE) : QColor(0x00,0x52,0x9B);
        val_color  = text_norm;
    } else {
        bg         = dark_mode_ ? QColor(0x2A,0x2A,0x2A) : Qt::white;
        name_color = text_dim;
        val_color  = text_norm;
    }

    // Apply via stylesheet so it coexists correctly with the global QSS
    QWidget* frame = c.name->parentWidget();
    frame->setStyleSheet(QString(
        "QWidget { background-color: %1; border: 1px solid %2; border-radius: 3px; }"
        "QLabel  { background: transparent; border: none; }"
    ).arg(bg.name(),
          (dark_mode_ ? QColor(0x3C,0x3C,0x3C) : QColor(0xDD,0xDD,0xDD)).name()));

    c.name->setStyleSheet(QString("color: %1;").arg(name_color.name()));
    c.value->setStyleSheet(QString("color: %1;").arg(val_color.name()));
}

void RegisterWidget::setPipelineState(const mips::PipelineState& state)
{
    // Decay all fade counters first
    for (int i = 1; i < 32; ++i)
        if (cells_[i].fade > 0) --cells_[i].fade;

    // Detect a register written in WB
    const auto& wb = state.stages[4];
    if (wb.valid && !wb.stalled && !wb.flushed && wb.raw != 0) {
        auto decoded = mips::Decoder::decode(wb.raw);
        if (decoded) {
            uint8_t dest = 0xFF;
            const auto& d = *decoded;
            if (d.format == mips::InstrFormat::R) {
                dest = d.r().rd;
            } else if (d.format == mips::InstrFormat::I) {
                if (d.opcode != mips::Opcode::SW && d.opcode != mips::Opcode::BEQ &&
                    d.opcode != mips::Opcode::BNE)
                    dest = d.i().rt;
            } else if (d.opcode == mips::Opcode::JAL) {
                dest = 31; // $ra
            }
            if (dest != 0xFF && dest != 0 && dest < 32) {
                cells_[dest].fade = 5;
            }
        }
    }

    // Detect registers read in ID
    read_rs_ = 0xFF;
    read_rt_ = 0xFF;
    const auto& id = state.stages[1];
    if (id.valid && id.raw != 0) {
        auto decoded = mips::Decoder::decode(id.raw);
        if (decoded) {
            const auto& d = *decoded;
            if (d.format == mips::InstrFormat::R) {
                read_rs_ = d.r().rs;
                read_rt_ = d.r().rt;
            } else if (d.format == mips::InstrFormat::I) {
                read_rs_ = d.i().rs;
                if (d.opcode == mips::Opcode::SW || d.opcode == mips::Opcode::BEQ ||
                    d.opcode == mips::Opcode::BNE)
                    read_rt_ = d.i().rt;
            }
        }
    }

    // Update displayed values from WB-committed register file snapshot.
    // We don't have direct register file access here; values are updated by
    // MainWindow via pipelineStateChanged which routes through the controller.
    // For now just refresh all cells to re-apply highlights.
    for (int i = 0; i < 32; ++i)
        updateCell(i);
}

void RegisterWidget::setShowAliases(bool show)
{
    show_aliases_ = show;
    for (int i = 0; i < 32; ++i) updateCell(i);
}

void RegisterWidget::setDarkMode(bool dark)
{
    dark_mode_ = dark;
    for (int i = 0; i < 32; ++i) updateCell(i);
}

void RegisterWidget::clear()
{
    values_.fill(0);
    read_rs_ = 0xFF;
    read_rt_ = 0xFF;
    for (auto& c : cells_) c.fade = 0;
    for (int i = 0; i < 32; ++i) updateCell(i);
}

void RegisterWidget::updateValues(const std::array<uint32_t, 32>& vals)
{
    values_ = vals;
    for (int i = 0; i < 32; ++i) updateCell(i);
}

} // namespace nsc::qt
