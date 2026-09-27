# PipelineTraceWidget

## 1. Class Overview

`PipelineTraceWidget` renders the classic instruction × cycle pipeline diagram — one row per dynamic instruction (each execution of an instruction gets its own row), one column per simulator cycle, each cell showing which stage (IF/ID/EX/MEM/WB) that instruction occupied in that cycle — as a scrolling 20-cycle window. It is the automatically generated equivalent of the pipeline diagrams found in Patterson & Hennessy.

`MainWindow` feeds it pipeline state once per cycle via `updateCycle()`.

## 2. Project Structure and Dependencies

Constructed once by `MainWindow::setupCentralWidget()` and added as the fourth tab.

Qt modules required:
- **Qt Widgets** — `QWidget` (base class), `QTableWidget`, `QTableWidgetItem`, `QHeaderView`, `QVBoxLayout`.

Project-internal types:
- `mips::PipelineState`, `mips::StageSnapshot`, `mips::Decoder` (`mips_core`) — used to determine, per cycle, which instruction occupies which stage, and to render a short mnemonic for each row's instruction label.
- `nsc::qt::scale::monoFont()` (`nsc_qt/ui_scale.h`) — supplies the table's font size.

## 3. Class Hierarchy and Role

`PipelineTraceWidget` inherits [`QWidget`](https://doc.qt.io/qt-6/qwidget.html) directly and is `final`, hosting a single [`QTableWidget`](https://doc.qt.io/qt-6/qtablewidget.html). `QWidget` derives from [`QObject`](https://doc.qt.io/qt-6/qobject.html); this class declares `Q_OBJECT` but has no signals or slots of its own. It overrides `showEvent()` to redraw a table that changed while hidden.

## 4. Public Methods

#### `explicit PipelineTraceWidget(QWidget* parent = nullptr)`
Builds the trace table (font, selection mode, alternating row colors, header sizing) and adds it to a `QVBoxLayout`.

#### `void updateCycle(const mips::PipelineState& state)`
The single per-cycle entry point. Columns are `state.cycle`, the simulator's own cycle number, so a cycle the controller did not report stays blank; a cycle that is not later than the last one (reset) or that jumps past the whole window (a throttled run) starts a new diagram. Rows are dynamic instances: for each stage snapshot that is valid, not stalled and not flushed — oldest stage first — the instance continues the row of the same PC that was last seen one stage earlier in an earlier cycle; otherwise a new row starts. So a short loop whose PC is in WB and IF in the same cycle shows both, on two rows. Rows whose every entry has left the window are dropped. The table is redrawn only while the widget is visible; a hidden widget keeps recording and redraws once in `showEvent()`.

#### `void clear()`
Clears all rows and the last-seen cycle, then redraws the (now empty) table. Called by `MainWindow::onReset()`.

#### `void setDarkMode(bool dark)`
Switches the stage-cell color palette and redraws the table to apply it immediately.

Redrawing reuses the table's items and header labels, updating them in place, and the two fonts are built once in the constructor; nothing is reallocated per cycle.

## 5. Ownership and Lifecycle

`PipelineTraceWidget` is constructed with `MainWindow` as its `QObject` parent and is deleted automatically as part of the widget tree. Its `rows_` vector is bounded by the pruning in `updateCycle()`: it holds only the instances visible in the 20-cycle window, each as a fixed five-entry array.

## 6. Thread Safety

**GUI-thread only**, as a `QWidget` subclass.

## 7. Inter-Class Interactions

- **Receives from `MainWindow`**: `updateCycle()` (every reported cycle, from `onPipelineStateChanged()`, whether or not the panel is visible), `clear()` (from `onReset()`), `setDarkMode()`.
- Does not use `QSettings` or any other global/shared state directly.
