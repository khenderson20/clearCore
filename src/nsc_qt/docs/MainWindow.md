# MainWindow

## 1. Class Overview

`MainWindow` is the top-level window of the clearCore Qt6 desktop GUI (`clearCore-gui`) — the window that appears when the application starts. It hosts a `QTabWidget` with six tabs (Datapath, Registers, Memory, Pipeline Trace, Code Editor, Statistics), a menu bar, and a toolbar, and it owns the single `SimulatorController` instance that drives the MIPS simulation for the entire application.

A developer reaches for this class exactly once, in `main()` — it is not meant to be instantiated more than once or embedded inside another widget. Its job is orchestration: it builds every child widget, wires their signals to its own private slots, and translates `SimulatorController` state changes into updates across all six tabs.

## 2. Project Structure and Dependencies

`MainWindow` is constructed once, in `main.cpp`, and shown with `show()` before the Qt event loop starts.

Qt modules required (via CMake `find_package(Qt6 COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)`):
- **Qt Widgets** — `QMainWindow`, `QTabWidget`, `QMenuBar`, `QToolBar`, `QAction`, `QLabel`, `QComboBox`, `QPushButton`, `QMessageBox`, `QFileDialog`, `QDialog`, `QDialogButtonBox`, `QFormLayout`, `QFrame`, `QGroupBox`, `QHBoxLayout`, `QVBoxLayout`, `QStatusBar`
- **Qt Core** — `QSettings`, `QTimer`, `QString`

Project-internal types it depends on:
- `SimulatorController` (`nsc_qt/simulator_controller.h`) — owns one instance; the bridge to `mips_core`.
- `DatapathWidget`, `RegisterWidget`, `MemoryWidget`, `PipelineTraceWidget`, `CodeEditor` (`nsc_qt/widgets/`) — the six tab contents (Statistics is built inline, not a separate class).
- `PreferencesDialog` (`nsc_qt/preferences_dialog.h`) — opened modally from the View menu.
- `assemble()` / `AssemblerResult` (`nsc_qt/assembler.h`) — used by the Code Editor tab's Assemble action.
- `exampleProgramCatalog()` / `ExampleProgram` (`nsc_qt/examples.h`) — populates the Code Editor's examples dropdown.
- `mips::PipelinedCpu`, `mips::load_hex_file`, `mips::Decoder`, `mips::register_abi_name` (`mips_core`) — used to construct the default processor and to decode instructions for the Stage Detail dialog.

Build requirement: linked into the `clearCore-gui` executable target in `CMakeLists.txt`, only built when `BUILD_QT6_UI` is `ON`.

## 3. Class Hierarchy and Role

`MainWindow` inherits [`QMainWindow`](https://doc.qt.io/qt-6/qmainwindow.html), which contributes the standard application-window layout: a menu bar, one or more toolbars, a central widget area, and a status bar, all managed automatically. `QMainWindow` itself derives from [`QWidget`](https://doc.qt.io/qt-6/qwidget.html), giving it a paintable, event-receiving top-level window, and ultimately [`QObject`](https://doc.qt.io/qt-6/qobject.html), which provides the meta-object system that `Q_OBJECT` activates — required here for the private slots and for `connect()` calls throughout the class.

## 4. Public Methods

#### `explicit MainWindow(QWidget* parent = nullptr)`

Constructs the main window. Creates a `SimulatorController` wrapping a fresh `mips::PipelinedCpu`, builds the menu bar, toolbar, and central tab widget, wires every signal connection described in [Inter-Class Interactions](#9-inter-class-interactions), then restores the persisted color scheme and execution speed from `QSettings("nsc-qt", "clearCore-gui")`. The `explicit` qualifier disables implicit `QWidget*`-to-`MainWindow` conversions. Call `show()` on the result before starting the Qt event loop — the constructor does not show the window itself.

## 5. Ownership and Lifecycle

`MainWindow` is designed to be a top-level window: `main.cpp` constructs it on the stack with no parent and calls `show()`, so its lifetime is tied to `main()`'s scope and to `QApplication::exec()`.

- `controller_` is owned via `std::unique_ptr<SimulatorController>` — destroyed automatically when `MainWindow` is destroyed.
- Every child widget (`tabs_`, `datapath_widget_`, `register_widget_`, `memory_widget_`, `trace_widget_`, `code_editor_`, `examples_combo_`, all `QLabel*`/`QAction*` members) is constructed with a `QObject` parent somewhere in the widget tree rooted at `MainWindow` (directly, or indirectly once `QTabWidget::addTab()` reparents a tab's widget to the tab widget). None of these pointers need manual deletion — Qt's parent-child ownership deletes the entire tree when `MainWindow` is destroyed.
- `PreferencesDialog` in `onShowPreferences()` is a local stack object (`PreferencesDialog dlg(this)`), not heap-allocated, so it is destroyed automatically when the method returns after `dlg.exec()`.

## 6. Thread Safety

**GUI-thread only.** As a `QWidget` subclass, every method must be called from the thread that owns the Qt GUI event loop. `MainWindow` does not create any threads itself; `SimulatorController`'s `QTimer`-driven `run()` mode also executes entirely on this same thread (see `SimulatorController.md`), so there are no cross-thread signal deliveries to reason about here.

## 7. Inter-Class Interactions

`MainWindow` is the hub that every other `nsc_qt` class ultimately connects through:

- **`SimulatorController` signals → private slots.** Connects `cycleExecuted`, `pipelineStateChanged`, `statisticsUpdated`, `halted`, `faulted`, and `breakpointHit` to its own private slots (`onCycleExecuted`, `onPipelineStateChanged`, `onStatisticsUpdated`, `onHalted`, `onFaulted`, and an inline lambda respectively). These slots fan the resulting state out to whichever tab widgets need it — for example `onPipelineStateChanged()` calls `setPipelineState()` on `DatapathWidget`, `updateCycle()` on `PipelineTraceWidget` and `RegisterWidget`, and `updateDisplay()` on `MemoryWidget` in the same call.
- **`DatapathWidget` signals → private slots.** Connects `breakpointToggleRequested` to `onBreakpointToggle()` and `stageDetailRequested` to `onStageDetailRequested()`.
- **`PreferencesDialog` signals → mixed targets.** When `onShowPreferences()` opens the dialog, it connects `colorSchemeChanged` to `applyColorScheme()`, `executionSpeedChanged` directly to `SimulatorController::setExecutionSpeed()`, `showRegisterAliasesChanged` directly to `RegisterWidget::setShowAliases()`, and `fontSizeChanged` to a lambda that updates `code_editor_`'s font.
- **`QComboBox::activated` → `onExampleSelected()`.** User selection in the examples dropdown loads a catalog entry into `code_editor_`.
- **`QSettings("nsc-qt", "clearCore-gui")`** is read once in the constructor (color scheme, execution speed) and written by `PreferencesDialog::saveSettings()` (a separate class, not by `MainWindow` itself).
- **`QFileDialog`** is used in `onOpenFile()` and `onSaveTrace()` for local file selection — no network or IPC involved.

All slots that receive these connections are `private`, so nothing outside `MainWindow` connects to them directly; the interactions above are internal wiring set up entirely within the class's own setup methods.
