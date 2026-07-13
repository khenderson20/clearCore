# DatapathWidget

## 1. Class Overview

`DatapathWidget` is the pipeline visualizer at the heart of the clearCore Qt6 GUI's Datapath tab — a live diagram of all five MIPS pipeline stages (IF, ID, EX, MEM, WB), each rendered as a colored box showing the PC, decoded instruction, and stall/flush/breakpoint state for that stage on the current cycle. It also renders forwarding-path arcs (EX/MEM→EX, MEM/WB→EX) above the boxes when forwarding is active.

A developer reaches for this class as one tab of `MainWindow`'s central `QTabWidget`; it is fed a `mips::PipelineState` snapshot once per simulated cycle via `setPipelineState()` and repaints itself accordingly. It also originates two user-facing interactions — toggling a breakpoint and opening a stage-detail dialog — via its own signals, from mouse clicks, a right-click context menu, or keyboard navigation.

## 2. Project Structure and Dependencies

Constructed once by `MainWindow::setupCentralWidget()` and added as the first tab.

Qt modules required:
- **Qt OpenGL / OpenGL Widgets** — `QOpenGLWidget`, `QOpenGLFunctions` (base classes).
- **Qt GUI** — `QPainter`, `QPainterPath`, `QPen`, `QColor`, `QFont` (all painting done via `QPainter`, not raw GL calls beyond `glViewport`).
- **Qt Widgets** — `QMenu`, `QAction`, `QDialog`, `QDialogButtonBox`, `QFormLayout`, `QLabel` (used to build the double-click Stage Detail dialog).
- **Qt Core / GUI events** — `QMouseEvent`, `QContextMenuEvent`, `QKeyEvent`, `QFocusEvent`.

Project-internal types:
- `mips::PipelineState`, `mips::StageSnapshot` (`mips_core`) — the per-cycle data this widget renders.
- `mips::Decoder`, `mips::register_abi_name` (`mips_core`) — used to decode instructions for both the inline stage-box text and the Stage Detail dialog.
- `nsc::qt::scale::monoFont()` (`nsc_qt/ui_scale.h`) — supplies all font sizes used in this widget.

## 3. Class Hierarchy and Role

`DatapathWidget` inherits [`QOpenGLWidget`](https://doc.qt.io/qt-6/qopenglwidget.html) and `protected QOpenGLFunctions` ([QOpenGLFunctions](https://doc.qt.io/qt-6/qopenglfunctions.html)). `QOpenGLWidget` provides an OpenGL-backed rendering surface with its own `paintGL()`/`initializeGL()`/`resizeGL()` virtual hooks, in place of the ordinary `QWidget::paintEvent()`. In this class, `QPainter` is used for all drawing inside `paintGL()` — the only direct GL call is `glViewport()` in `resizeGL()`, and `QOpenGLFunctions` is inherited but not otherwise used. `QOpenGLWidget` derives from [`QWidget`](https://doc.qt.io/qt-6/qwidget.html) → [`QObject`](https://doc.qt.io/qt-6/qobject.html).

## 4. Signals

#### `breakpointToggleRequested(uint32_t pc)`
Emitted when the user right-clicks a valid stage and confirms "Set Breakpoint"/"Clear Breakpoint" from the context menu, or presses Space/B on the keyboard-selected stage. Carries the PC of the instruction currently in that stage. `MainWindow` connects this to `onBreakpointToggle()`, which updates `SimulatorController`'s breakpoint set and calls `setBreakpoints()` back on this widget.

#### `stageDetailRequested(int stage_index, uint32_t pc, uint32_t raw_instr)`
Emitted when the user double-clicks a valid stage box, or presses Enter/Return on the keyboard-selected stage. Carries the stage index (0=IF … 4=WB), the instruction's PC, and its raw 32-bit encoding. `MainWindow` connects this to `onStageDetailRequested()`, which opens a modal dialog showing the fully decoded instruction and live register values.

## 5. Public Methods

#### `explicit DatapathWidget(QWidget* parent = nullptr)`
Sets a minimum size sized to fit five stage boxes at their minimum dimensions, and sets the focus policy to `Qt::StrongFocus` so the widget can receive keyboard focus by click or Tab.

#### `void setPipelineState(const mips::PipelineState& state)`
Stores `state` and calls `update()` to trigger a repaint. Call this once per simulated cycle.

#### `void setBreakpoints(const std::unordered_set<uint32_t>& bps)`
Replaces the widget's local copy of the breakpoint set (used only to decide which stage boxes get the breakpoint marker) and repaints. `SimulatorController` remains the source of truth; this is purely a rendering cache.

#### `void setDarkMode(bool dark)`
Switches the stage-box color palette and repaints.

## 6. Protected Virtual Methods / Event Handlers

#### `void initializeGL() override`
Base class: `QOpenGLWidget`. Calls `initializeOpenGLFunctions()`. No other GL setup is performed since rendering is done via `QPainter`.

#### `void resizeGL(int w, int h) override`
Base class: `QOpenGLWidget`. Calls `glViewport(0, 0, w, h)` — the only raw OpenGL call in the class.

#### `void paintGL() override`
Base class: `QOpenGLWidget`. Draws the entire widget via `QPainter`: background, title bar with cycle count, a separator line, flow arrows between stages, forwarding arcs, all five stage boxes, the keyboard focus ring (if focused), and the forwarding legend.

#### `void mousePressEvent(QMouseEvent* ev) override`
Base class: `QWidget` (via `QOpenGLWidget`). Requests keyboard focus, updates `selected_stage_` to whichever stage box was clicked (if any), then forwards the event to the base implementation.

#### `void mouseDoubleClickEvent(QMouseEvent* ev) override`
Base class: `QWidget`. If the double-click lands on a stage box containing a valid instruction, emits `stageDetailRequested`.

#### `void contextMenuEvent(QContextMenuEvent* ev) override`
Base class: `QWidget`. If the right-click lands on a stage box containing a valid instruction, shows a one-item `QMenu` ("Set Breakpoint" or "Clear Breakpoint" depending on current state) and emits `breakpointToggleRequested` if chosen.

#### `void keyPressEvent(QKeyEvent* ev) override`
Base class: `QWidget`. Left/Right move `selected_stage_` (wrapping across the five stages); Enter/Return emits `stageDetailRequested` for the selected stage if valid; Space/B emits `breakpointToggleRequested` for the selected stage if valid. Any other key is forwarded to `QOpenGLWidget::keyPressEvent()`.

#### `void focusInEvent(QFocusEvent* ev) override` / `void focusOutEvent(QFocusEvent* ev) override`
Base class: `QWidget`. Forward to the base implementation, then call `update()` so the focus ring is drawn or removed.

## 7. Ownership and Lifecycle

`DatapathWidget` is constructed with `MainWindow` (or its central tab widget, once `QTabWidget::addTab()` reparents it) as its `QObject` parent, so it is deleted automatically as part of the widget tree — no manual deletion is required or expected. It is embedded as a tab, not shown as a top-level window.

## 8. Thread Safety

**GUI-thread only**, as a `QOpenGLWidget`/`QWidget` subclass. All painting happens in `paintGL()`, called by Qt's own paint scheduling on the GUI thread.

## 9. Inter-Class Interactions

- **Receives from `MainWindow`**: `setPipelineState()` (every cycle, forwarded from `SimulatorController::pipelineStateChanged`), `setBreakpoints()` (after any breakpoint toggle), `setDarkMode()` (from `applyColorScheme()`).
- **Emits to `MainWindow`**: `breakpointToggleRequested`, `stageDetailRequested` — both connected in `MainWindow::setupConnections()`.
- Does not use `QSettings` or any other global/shared state directly.
