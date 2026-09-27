#include "nsc_qt/widgets/datapath_widget.h"
#include "mips/decoder.h"
#include "mips/registers.h"
#include "nsc_qt/instr_format.h"
#include "nsc_qt/ui_scale.h"

#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFocusEvent>
#include <QFormLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QString>
#include <QVBoxLayout>
#include <algorithm>
#include <sstream>
#include <string>
#include <unordered_set>

namespace nsc::qt {

namespace {

static const QColor STAGE_COLORS_LIGHT[5] = {
    QColor("#E3F2FD"),  // IF  – light blue
    QColor("#E0F7FA"),  // ID  – light cyan
    QColor("#E8F5E9"),  // EX  – light green
    QColor("#FFFDE7"),  // MEM – light yellow
    QColor("#FFEBEE"),  // WB  – light pink/red
};
static const QColor STAGE_COLORS_DARK[5] = {
    QColor("#0D47A1"), QColor("#006064"), QColor("#1B5E20"), QColor("#F57F17"), QColor("#B71C1C"),
};

static const char* STAGE_NAMES[5] = {"IF", "ID", "EX", "MEM", "WB"};

}  // anonymous namespace

DatapathWidget::DatapathWidget(QWidget* parent) : QOpenGLWidget(parent) {
    setMinimumSize(5 * BOX_W_MIN + 4 * GAP_MIN + 40, BOX_H_MIN + 120);

    // Keyboard access: Left/Right select a stage, Enter opens stage detail,
    // Space/B toggles a breakpoint. Without StrongFocus this widget never
    // enters the tab order and those actions are mouse-only (audit Critical #1).
    setFocusPolicy(Qt::StrongFocus);
}

void DatapathWidget::setPipelineState(const mips::PipelineState& state) {
    state_ = state;
    update();
}

void DatapathWidget::setBreakpoints(const std::unordered_set<uint32_t>& bps) {
    breakpoints_ = bps;
    update();
}

void DatapathWidget::setDarkMode(bool dark) {
    dark_mode_ = dark;
    update();
}

void DatapathWidget::initializeGL() {
    initializeOpenGLFunctions();
}

void DatapathWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
}

void DatapathWidget::paintGL() {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Background
    const QColor bg_color = dark_mode_ ? QColor(0x1E, 0x1E, 0x1E) : QColor(0xF5, 0xF5, 0xF5);
    p.fillRect(rect(), bg_color);

    // Title bar
    const QColor title_bg = dark_mode_ ? QColor(0x25, 0x25, 0x26) : QColor(0xEB, 0xEB, 0xEB);
    p.fillRect(QRect(0, 0, width(), 28), title_bg);
    p.setPen(dark_mode_ ? QColor(0xCC, 0xCC, 0xCC) : QColor(0x33, 0x33, 0x33));
    p.setFont(scale::monoFont(scale::kFontSizeBody, true));
    p.drawText(QRect(0, 0, width() - 8, 28), Qt::AlignVCenter | Qt::AlignRight,
               tr("Cycle  %1").arg(static_cast<qulonglong>(state_.cycle)));
    p.setPen(dark_mode_ ? QColor(0x9C, 0xDC, 0xFE) : QColor(0x00, 0x78, 0xD4));
    p.setFont(scale::monoFont(scale::kFontSizeBody, true));
    p.drawText(QRect(8, 0, width(), 28), Qt::AlignVCenter | Qt::AlignLeft, tr("Pipeline Datapath"));

    // Separator below title
    const QColor sep_color = dark_mode_ ? QColor(0x44, 0x44, 0x44) : QColor(0xDD, 0xDD, 0xDD);
    p.setPen(QPen(sep_color, 1));
    p.drawLine(0, 28, width(), 28);

    // Flow arrows between stage boxes (draw first, behind boxes)
    drawFlowArrows(p);

    // Forwarding arcs above boxes
    drawForwardingArrows(p);

    // Stage boxes
    for (int i = 0; i < 5; ++i)
        drawStageBox(p, i, state_.stages[static_cast<std::size_t>(i)]);

    // Keyboard focus ring, drawn last so it sits above everything else.
    if (hasFocus()) drawFocusRing(p, selected_stage_);

    // Forwarding legend at bottom-left when active
    const bool has_fwd = state_.fwd_ex_to_ex_a || state_.fwd_ex_to_ex_b || state_.fwd_mem_to_ex_a ||
                         state_.fwd_mem_to_ex_b;
    if (has_fwd) {
        const int lx = 8, ly = height() - 40;
        p.setFont(scale::monoFont(scale::kFontSizeDense, true));
        p.setPen(QColor("#FF6F00"));
        p.drawText(lx, ly, tr("── EX/MEM→EX forwarding"));
        p.setPen(QColor("#7B1FA2"));
        p.drawText(lx, ly + 18, tr("── MEM/WB→EX forwarding"));
    }
}

QRect DatapathWidget::stageRect(int idx) const {
    constexpr int MARGIN_X = 24;
    constexpr int TITLE_H  = 32;  // title bar + separator
    constexpr int BOTTOM_H = 48;  // legend + padding

    const int avail_w = width() - 2 * MARGIN_X;
    const int avail_h = height() - TITLE_H - BOTTOM_H;

    // Gap scales: ~4.5% of available width per gap, min 16px
    const int gap = (std::max)(GAP_MIN, avail_w / 22);
    // Box width fills remaining space equally across 5 stages
    const int box_w = (std::max)(BOX_W_MIN, (avail_w - 4 * gap) / 5);
    // Height: 80% of box width, clamped so it fits vertically
    const int box_h = std::clamp(box_w * 4 / 5, BOX_H_MIN, avail_h);

    const int total_w = 5 * box_w + 4 * gap;
    const int x0      = (width() - total_w) / 2;
    // Centre vertically in the available space below the title bar
    const int y0 = TITLE_H + (avail_h - box_h) / 2;

    return {x0 + idx * (box_w + gap), y0, box_w, box_h};
}

int DatapathWidget::stageAtPos(QPoint pos) const {
    for (int i = 0; i < 5; ++i)
        if (stageRect(i).contains(pos)) return i;
    return -1;
}

void DatapathWidget::drawFlowArrows(QPainter& p) const {
    const QColor shaft_color = dark_mode_ ? QColor(0x55, 0x55, 0x55) : QColor(0xBB, 0xBB, 0xBB);
    p.setBrush(shaft_color);

    for (int i = 0; i < 4; ++i) {
        const QRect left  = stageRect(i);
        const QRect right = stageRect(i + 1);
        const int   y     = left.center().y();
        const int   x1    = left.right() + 2;
        const int   x2    = right.left() - 2;

        // Shaft
        p.setPen(QPen(shaft_color, 2));
        p.drawLine(x1, y, x2 - 8, y);

        // Arrowhead
        const QPoint head[3] = {
            QPoint(x2, y),
            QPoint(x2 - 8, y - 5),
            QPoint(x2 - 8, y + 5),
        };
        p.setPen(Qt::NoPen);
        p.drawPolygon(head, 3);
    }
}

void DatapathWidget::drawStageBox(QPainter& p, int idx, const mips::StageSnapshot& snap) const {
    const QRect   r           = stageRect(idx);
    const QColor& stage_color = dark_mode_ ? STAGE_COLORS_DARK[idx] : STAGE_COLORS_LIGHT[idx];
    const QColor  box_bg = snap.valid
                               ? stage_color
                               : (dark_mode_ ? QColor(0x2C, 0x2C, 0x2C) : QColor(0xE8, 0xE8, 0xE8));

    // Drop shadow (subtle)
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, dark_mode_ ? 60 : 30));
    p.drawRoundedRect(r.adjusted(3, 3, 3, 3), 7, 7);

    // Box fill
    p.setBrush(box_bg);
    p.setPen(QPen(dark_mode_ ? QColor(0x55, 0x55, 0x55) : QColor(0xBB, 0xBB, 0xBB), 1));
    p.drawRoundedRect(r, 6, 6);

    // ── Header band ────────────────────────────────────────────────────────────
    const int   header_h = (std::max)(24, r.height() / 4);
    const QRect header_r(r.x(), r.y(), r.width(), header_h);

    // Clip to box so rounded corners at top are preserved
    QPainterPath clip;
    clip.addRoundedRect(r, 6, 6);
    p.setClipPath(clip);

    const QColor header_bg =
        snap.valid ? (dark_mode_ ? stage_color.darker(160) : stage_color.darker(120))
                   : (dark_mode_ ? QColor(0x22, 0x22, 0x22) : QColor(0xD8, 0xD8, 0xD8));
    p.fillRect(header_r, header_bg);
    p.setClipping(false);

    // Stage name -- clamped to the shared scale rather than an ad hoc
    // width-derived size (audit Warning: no modular scale / too-small text).
    const int name_font_size = (std::max)(scale::kFontSizeBody, r.width() / 14);
    p.setPen(dark_mode_ ? Qt::white : Qt::black);
    p.setFont(scale::monoFont(name_font_size, true));
    p.drawText(header_r, Qt::AlignCenter, QString(STAGE_NAMES[idx]));

    // ── Inactive state ─────────────────────────────────────────────────────────
    if (!snap.valid) {
        p.setPen(dark_mode_ ? QColor(0x66, 0x66, 0x66) : QColor(0xAA, 0xAA, 0xAA));
        p.setFont(scale::monoFont((std::max)(scale::kFontSizeDense, r.width() / 18)));
        p.drawText(r.adjusted(0, header_h, 0, 0), Qt::AlignCenter,
                   snap.stalled   ? tr("STALL")
                   : snap.flushed ? tr("FLUSH")
                                  : tr("---"));
        return;
    }

    // ── Content area ───────────────────────────────────────────────────────────
    const int content_top  = r.y() + header_h + 4;
    const int content_pad  = (std::max)(4, r.width() / 30);
    const int body_font_sz = (std::max)(scale::kFontSizeDense, r.width() / 18);

    // PC label
    p.setPen(dark_mode_ ? QColor(0x9C, 0xDC, 0xFE) : QColor(0x00, 0x52, 0x9B));
    p.setFont(scale::monoFont(body_font_sz));
    p.drawText(QRect(r.x() + content_pad, content_top, r.width() - content_pad * 2, 18),
               Qt::AlignLeft | Qt::AlignVCenter, tr("PC: 0x%1").arg(snap.pc, 8, 16, QChar('0')));

    // Stall/Flush badge (top-right corner of content)
    if (snap.stalled || snap.flushed) {
        p.setPen(snap.stalled ? QColor("#E65100") : QColor("#880E4F"));
        p.setFont(scale::monoFont(body_font_sz, true));
        p.drawText(QRect(r.x() + content_pad, content_top, r.width() - content_pad * 2, 18),
                   Qt::AlignRight | Qt::AlignVCenter, snap.stalled ? tr("STALL") : tr("FLUSH"));
    }

    // Decoded instruction text (assembly notation -- not translated, see format_instr)
    if (snap.raw != 0) {
        const std::string decoded = format_instr(snap.raw);
        p.setPen(dark_mode_ ? QColor(0xE0, 0xE0, 0xE0) : QColor(0x1A, 0x1A, 0x1A));
        p.setFont(scale::monoFont(body_font_sz));
        p.drawText(QRect(r.x() + content_pad, content_top + 20, r.width() - content_pad * 2,
                         r.bottom() - content_top - 24),
                   Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap,
                   QString::fromStdString(decoded));
    }

    // Breakpoint indicator: a red ring with a punched-out center ("bullseye"),
    // not a plain filled dot -- gives the marker a shape distinct from any
    // other color-coded element in this widget (audit Warning: color-only cue).
    if (breakpoints_.count(snap.pc)) {
        const int    outer = 14, inner = 6;
        const QPoint c(r.right() - outer / 2 - 5, r.y() + 5 + outer / 2);
        p.setBrush(QColor("#E53935"));
        p.setPen(QPen(dark_mode_ ? QColor(0xAA, 0x00, 0x00) : Qt::darkRed, 1));
        p.drawEllipse(c, outer / 2, outer / 2);
        p.setPen(Qt::NoPen);
        p.setBrush(box_bg);
        p.drawEllipse(c, inner / 2, inner / 2);
    }
}

void DatapathWidget::drawForwardingArrows(QPainter& p) const {
    // Draw an arc above the pipeline boxes for each active forwarding path.
    auto drawArc = [&](int from_idx, int to_idx, const QColor& color) {
        const QRect  from_r = stageRect(from_idx);
        const QRect  to_r   = stageRect(to_idx);
        const int    arc_y  = from_r.top() - 10;
        const QPoint start(from_r.center().x(), arc_y);
        const QPoint end(to_r.center().x(), arc_y);
        const int    lift = (std::max)(20, (from_r.top() - 34) / 2);

        QPainterPath path;
        path.moveTo(start);
        path.cubicTo(start + QPoint(0, -lift), end + QPoint(0, -lift), end);

        // Glow under
        p.setPen(QPen(QColor(color.red(), color.green(), color.blue(), 60), 5, Qt::SolidLine,
                      Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);

        // Main arc
        p.setPen(QPen(color, 2, Qt::DashLine, Qt::RoundCap));
        p.drawPath(path);

        // Arrowhead at destination
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        const QPoint tip(end.x(), end.y() + 1);
        const QPoint head[3] = {tip, tip + QPoint(-5, -8), tip + QPoint(5, -8)};
        p.drawPolygon(head, 3);
    };

    if (state_.fwd_ex_to_ex_a || state_.fwd_ex_to_ex_b)
        drawArc(3, 2, QColor("#FF6F00"));  // EX/MEM → EX
    if (state_.fwd_mem_to_ex_a || state_.fwd_mem_to_ex_b)
        drawArc(4, 2, QColor("#7B1FA2"));  // MEM/WB → EX
}

void DatapathWidget::drawFocusRing(QPainter& p, int idx) const {
    const QRect r = stageRect(idx).adjusted(-4, -4, 4, 4);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(dark_mode_ ? QColor("#4FC3F7") : QColor("#0078D4"), 2, Qt::DashLine));
    p.drawRoundedRect(r, 9, 9);
}

void DatapathWidget::mousePressEvent(QMouseEvent* ev) {
    setFocus(Qt::MouseFocusReason);
    const int idx = stageAtPos(ev->pos());
    if (idx >= 0) {
        selected_stage_ = idx;
        update();
    }
    QOpenGLWidget::mousePressEvent(ev);
}

void DatapathWidget::mouseDoubleClickEvent(QMouseEvent* ev) {
    const int idx = stageAtPos(ev->pos());
    if (idx < 0) return;
    const auto& snap = state_.stages[static_cast<std::size_t>(idx)];
    if (!snap.valid) return;
    emit stageDetailRequested(idx, snap.pc, snap.raw);
}

void DatapathWidget::contextMenuEvent(QContextMenuEvent* ev) {
    const int idx = stageAtPos(ev->pos());
    if (idx < 0) return;
    const auto& snap = state_.stages[static_cast<std::size_t>(idx)];
    if (!snap.valid) return;

    QMenu      menu(this);
    const bool has_bp = breakpoints_.count(snap.pc) > 0;
    QAction*   act    = menu.addAction(has_bp ? tr("Clear Breakpoint") : tr("Set Breakpoint"));
    if (menu.exec(ev->globalPos()) == act) emit breakpointToggleRequested(snap.pc);
}

void DatapathWidget::keyPressEvent(QKeyEvent* ev) {
    switch (ev->key()) {
    case Qt::Key_Left:
        selected_stage_ = (selected_stage_ + 4) % 5;  // wrap backwards
        update();
        ev->accept();
        return;
    case Qt::Key_Right:
        selected_stage_ = (selected_stage_ + 1) % 5;
        update();
        ev->accept();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter: {
        const auto& snap = state_.stages[static_cast<std::size_t>(selected_stage_)];
        if (snap.valid) emit stageDetailRequested(selected_stage_, snap.pc, snap.raw);
        ev->accept();
        return;
    }
    case Qt::Key_Space:
    case Qt::Key_B: {
        const auto& snap = state_.stages[static_cast<std::size_t>(selected_stage_)];
        if (snap.valid) emit breakpointToggleRequested(snap.pc);
        ev->accept();
        return;
    }
    default:
        QOpenGLWidget::keyPressEvent(ev);
    }
}

void DatapathWidget::focusInEvent(QFocusEvent* ev) {
    QOpenGLWidget::focusInEvent(ev);
    update();  // draw the focus ring
}

void DatapathWidget::focusOutEvent(QFocusEvent* ev) {
    QOpenGLWidget::focusOutEvent(ev);
    update();  // hide the focus ring
}

}  // namespace nsc::qt
