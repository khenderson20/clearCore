#pragma once

#include "mips/processor.h"
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QPoint>
#include <unordered_set>

class QKeyEvent;
class QFocusEvent;

namespace nsc::qt {

class DatapathWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

public:
    explicit DatapathWidget(QWidget* parent = nullptr);

    void setPipelineState(const mips::PipelineState& state);
    void setBreakpoints(const std::unordered_set<uint32_t>& bps);
    void setDarkMode(bool dark);

signals:
    void breakpointToggleRequested(uint32_t pc);
    void stageDetailRequested(int stage_index, uint32_t pc, uint32_t raw_instr);

protected:
    void initializeGL()             override;
    void resizeGL(int w, int h)     override;
    void paintGL()                  override;
    void mousePressEvent(QMouseEvent* ev)        override;
    void mouseDoubleClickEvent(QMouseEvent* ev)  override;
    void contextMenuEvent(QContextMenuEvent* ev) override;

    // Keyboard access for the stage selection this widget represents --
    // Left/Right move the selection, Enter/Return opens stage detail,
    // Space/B toggles a breakpoint on the selected stage. Without these,
    // breakpoints and stage detail were mouse-only (audit Critical #1).
    void keyPressEvent(QKeyEvent* ev)   override;
    void focusInEvent(QFocusEvent* ev)  override;
    void focusOutEvent(QFocusEvent* ev) override;

private:
    // Returns the stage index (0–4) at widget position, or -1 if none.
    int stageAtPos(QPoint pos) const;
    QRect stageRect(int idx) const;

    void drawStageBox(QPainter& p, int idx, const mips::StageSnapshot& snap) const;
    void drawFlowArrows(QPainter& p) const;
    void drawForwardingArrows(QPainter& p) const;
    void drawFocusRing(QPainter& p, int idx) const;

    mips::PipelineState          state_{};
    std::unordered_set<uint32_t> breakpoints_{};
    bool                         dark_mode_ = false;

    // Keyboard-selected stage (0-4). Always valid; the focus ring is only
    // drawn while the widget actually has keyboard focus.
    int selected_stage_ = 0;

    // Minimum box dimensions used for setMinimumSize() only.
    // Actual paint dimensions are computed dynamically in stageRect().
    static constexpr int BOX_W_MIN = 120;
    static constexpr int BOX_H_MIN = 100;
    static constexpr int GAP_MIN   = 16;
};

} // namespace nsc::qt