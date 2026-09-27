# RegisterWidget

## 1. Class Overview

`RegisterWidget` renders all 32 MIPS registers in a 4×8 grid for the Registers tab, showing each register's ABI alias (`$t0`, `$sp`, …) and current hex value. It highlights the register(s) read by the instruction currently in the ID stage (a cyan tint) and briefly highlights any register just written in WB with a green fade that decays over a fixed wall-clock duration, independent of simulation speed.

Internally it defines a private nested `Cell` class — one per register — that paints its own rounded background and border directly in `paintEvent()` rather than going through `QWidget::setStyleSheet()`, which would otherwise reparse a full stylesheet string on every one of 32 cells, on every simulated cycle. This is an implementation detail, not part of the public API.

A developer reaches for this class as the content of `MainWindow`'s Registers dock panel, feeding it fresh pipeline state and register values once per cycle via `updateCycle()`.

## 2. Project Structure and Dependencies

Constructed once by `MainWindow::setupCentralWidget()` and placed in the Registers dock, the front tab of the bottom dock area.

Qt modules required:
- **Qt Widgets** — `QWidget` (base class), `QGridLayout`, `QLabel`.
- **Qt GUI** — `QPainter`, `QPalette`, `QColor`, `QFont` (used by the private `Cell` class for direct painting).
- **Qt Core** — `QDateTime` (wall-clock timestamps for the fade animation), `QTimer` (drives the fade repaint loop).

Project-internal types:
- `mips::PipelineState`, `mips::Decoder`, `mips::register_abi_name` (`mips_core`) — used to decode the ID and WB instructions and to render alias names.
- `mips::source_registers()` and `mips::destination_register()` (`mips/pipeline_units.h`) — the pipeline's own read and write rules, so the highlights match what the hazard and forwarding units see: a shift does not light `rs`, `lui` lights nothing, and `mfc0` lights `rt` (never the CP0 register number in `rd`).
- `nsc::qt::scale::monoFont()` (`nsc_qt/ui_scale.h`) — supplies the font sizes used for register names and values.

## 3. Class Hierarchy and Role

`RegisterWidget` inherits [`QWidget`](https://doc.qt.io/qt-6/qwidget.html) directly (not a specialized Qt Widgets class) and is `final`, building its own grid of 32 child `Cell` widgets via `QGridLayout`. `QWidget` derives from [`QObject`](https://doc.qt.io/qt-6/qobject.html), giving it `Q_OBJECT` support for the internal `fade_timer_` connection. It overrides `showEvent()` to refresh cells whose values changed while the widget was hidden.

## 4. Public Methods

#### `explicit RegisterWidget(QWidget* parent = nullptr)`
Builds the header bar and the 4×8 grid of 32 `Cell` widgets via `buildGrid()`, and constructs (but does not start) an internal `fade_timer_` with a 33ms interval (~30fps), connected to a lambda that advances the fade animation for any cell currently fading.

#### `void updateCycle(const mips::PipelineState& state, const std::array<uint32_t, 32>& vals)`
The single per-cycle entry point. Stores `vals` and the registers read by the ID-stage instruction (the cyan highlight). If the widget is hidden (its dock is closed or its tab is not in front) it stops there, and `showEvent()` refreshes the cells once. Otherwise it starts the fade for the register written in WB via `startFade()` and refreshes every one of the 32 cells exactly once.

#### `void setShowAliases(bool show)`
Toggles whether register names include their ABI alias (e.g. `$8 (t0)` vs. `$8`) and refreshes all cells.

#### `void setDarkMode(bool dark)`
Switches the cell color palette and refreshes all cells.

#### `void clear()`
Zeroes all displayed values, clears the ID-stage read highlight, cancels any in-progress fade animations, stops `fade_timer_`, and refreshes all cells. Called by `MainWindow::onReset()`.

#### `uint32_t value(int idx) const noexcept`
Returns the currently displayed value of register `idx` (0–31). Intended for tests (`qt_ui_test.cpp` uses this to verify `clear()` behavior) rather than general application use.

## 5. Ownership and Lifecycle

`RegisterWidget` is constructed with `MainWindow` (or its tab widget, after reparenting) as its `QObject` parent and is deleted automatically as part of the widget tree. Its 32 `Cell` children and the `fade_timer_` are, in turn, constructed with `RegisterWidget` (or the grid container) as their own parent, so no manual cleanup of any of these is required.

## 6. Thread Safety

**GUI-thread only**, as a `QWidget` subclass. The fade animation's timestamps are read via `QDateTime::currentMSecsSinceEpoch()` on the GUI thread only — there is no cross-thread access to worry about.

## 7. Inter-Class Interactions

- **Receives from `MainWindow`**: `updateCycle()` (every cycle, called from `onPipelineStateChanged()` after it gathers register values from `SimulatorController::registerValue()`, whether or not the panel is visible), `setDarkMode()`, `clear()` (from `onReset()`).
- **Receives from `PreferencesDialog`**: `setShowAliases()` is connected directly to `PreferencesDialog::showRegisterAliasesChanged` in `MainWindow::onShowPreferences()`.
- Does not use `QSettings` or any other global/shared state directly.
