# PipelineTraceWidget

## 1. Class Overview

`PipelineTraceWidget` renders the classic instruction × cycle pipeline diagram — one row per in-flight instruction, one column per visible cycle, each cell showing which stage (IF/ID/EX/MEM/WB) that instruction occupied that cycle — as a scrolling 20-cycle window. It is the automatically-generated equivalent of the pipeline diagrams found in Patterson & Hennessy.

A developer reaches for this class as one tab of `MainWindow`'s central `QTabWidget`, feeding it pipeline state once per cycle via `updateCycle()`.

## 2. Project Structure and Dependencies

Constructed once by `MainWindow::setupCentralWidget()` and added as the fourth tab.

Qt modules required:
- **Qt Widgets** — `QWidget` (base class), `QTableWidget`, `QTableWidgetItem`, `QHeaderView`, `QVBoxLayout`.

Project-internal types:
- `mips::PipelineState`, `mips::StageSnapshot`, `mips::Decoder`, `mips::register_abi_name` (`mips_core`) — used to determine, per cycle, which instruction occupies which stage, and to render a short mnemonic for each row's instruction label.
- `nsc::qt::scale::monoFont()` (`nsc_qt/ui_scale.h`) — supplies the table's font size.

## 3. Class Hierarchy and Role

`PipelineTraceWidget` inherits [`QWidget`](https://doc.qt.io/qt-6/qwidget.html) directly, hosting a single [`QTableWidget`](https://doc.qt.io/qt-6/qtablewidget.html). `QWidget` derives from [`QObject`](https://doc.qt.io/qt-6/qobject.html); this class declares `Q_OBJECT` but has no signals or slots of its own.

## 4. Public Methods

#### `explicit PipelineTraceWidget(QWidget* parent = nullptr)`
Builds the trace table (font, selection mode, alternating row colors, header sizing) and adds it to a `QVBoxLayout`.

#### `void updateCycle(const mips::PipelineState& state)`
The single per-cycle entry point. Advances the internal cycle counter; for each of the five stage snapshots that is valid, not stalled, and not flushed, finds or creates the row for that instruction's PC and records `(current_cycle_, stage_name)` in that row's `stages` deque. Then prunes every row's entries older than the visible 20-cycle window and drops any row left with no visible entries, before calling `rebuildTable()`. Entries are keyed by absolute cycle number rather than a window-relative column offset specifically so that already-recorded entries stay correctly aligned with the header once `cycle_base_` advances past the first 20 cycles.

#### `void clear()`
Clears all rows and resets both cycle counters to zero, then rebuilds the (now-empty) table. Called by `MainWindow::onReset()`.

#### `void setDarkMode(bool dark)`
Switches the stage-cell color palette and rebuilds the table to apply it immediately.

## 5. Ownership and Lifecycle

`PipelineTraceWidget` is constructed with `MainWindow` (or its tab widget, after reparenting) as its `QObject` parent and is deleted automatically as part of the widget tree. Its `rows_` vector and each row's `stages` deque are bounded by the pruning logic in `updateCycle()` — they do not grow for the life of a run, only for the life of the 20-cycle visible window.

## 6. Thread Safety

**GUI-thread only**, as a `QWidget` subclass.

## 7. Inter-Class Interactions

- **Receives from `MainWindow`**: `updateCycle()` (every cycle, from `onPipelineStateChanged()`), `clear()` (from `onReset()`), `setDarkMode()`.
- Does not use `QSettings` or any other global/shared state directly.
