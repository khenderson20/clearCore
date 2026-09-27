# MemoryWidget

## 1. Class Overview

`MemoryWidget` renders a scrollable hex dump of simulator memory for the Memory tab — 16 rows of 16 bytes each, starting from a user-editable base address, with an ASCII column and a "Jump to 0x0" quick-jump chip alongside the base-address spin box.

A developer reaches for this class as one tab of `MainWindow`'s central `QTabWidget`, feeding it a `mips::Memory` reference once per cycle via `updateDisplay()`.

## 2. Project Structure and Dependencies

Constructed once by `MainWindow::setupCentralWidget()` and added as the third tab.

Qt modules required:
- **Qt Widgets** — `QWidget` (base class), `QTableWidget`, `QTableWidgetItem`, `QSpinBox`, `QLabel`, `QPushButton`, `QHBoxLayout`, `QVBoxLayout`, `QHeaderView`.

Project-internal types:
- `mips::Memory` (`mips_core`) — the byte-addressable memory this widget reads from via `read_byte()`.
- `nsc::qt::scale::monoFont()` (`nsc_qt/ui_scale.h`) — supplies the font sizes used for the table and nav bar.

## 3. Class Hierarchy and Role

`MemoryWidget` inherits [`QWidget`](https://doc.qt.io/qt-6/qwidget.html) directly, hosting a [`QTableWidget`](https://doc.qt.io/qt-6/qtablewidget.html) for the hex grid. `QWidget` derives from [`QObject`](https://doc.qt.io/qt-6/qobject.html), giving it `Q_OBJECT` support for the `onAddressChanged` slot.

## 4. Public Methods

#### `explicit MemoryWidget(QWidget* parent = nullptr)`
Builds the nav bar (the "Jump to 0x0" button, a "Base address:" label, the base-address `QSpinBox`, and a status label) and the 16×18 hex table via `buildTable()`. Connects the "Jump to 0x0" button's `clicked()` to a lambda that sets the address spin box to 0, and the spin box's `valueChanged` to `onAddressChanged()`.

#### `void updateDisplay(const mips::Memory& mem)`
Stores a pointer to `mem` (see [Ownership and Lifecycle](#6-ownership-and-lifecycle)) and calls `refreshRows()` to repaint the table from the current base address. Called once per cycle from `MainWindow::onPipelineStateChanged()`.

#### `void markWritten(uint32_t addr)`
Adds `addr` to an internal set of "recently written" byte addresses, which `refreshRows()` highlights in yellow on the next repaint before clearing the set. **Not currently called from anywhere in the codebase** — the highlighting logic is fully implemented and exercised by `refreshRows()`, but no caller currently supplies write addresses, so this feature is presently inert. Wiring it up would mean having `MainWindow` (or `SimulatorController`) report the address(es) written by the WB-stage instruction each cycle, the same way `RegisterWidget::updateCycle()` is told which register was written.

#### `void setDarkMode(bool dark)`
Switches the table's color palette and, if a memory reference has previously been supplied via `updateDisplay()`, refreshes the table immediately.

## 5. Protected / Private Slots

#### `void onAddressChanged(int value)` *(private slot)*
Connected to the base-address spin box's `valueChanged(int)` signal. Aligns `value` down to a 16-byte boundary and stores it as the new base address, then refreshes the table if a memory reference is available.

## 6. Ownership and Lifecycle

`MemoryWidget` is constructed with `MainWindow` (or its tab widget, after reparenting) as its `QObject` parent and is deleted automatically as part of the widget tree.

`last_mem_` is a raw, **non-owning** `const mips::Memory*` set by `updateDisplay()`. Its validity depends entirely on the lifetime of the `mips::IProcessor` owned by `SimulatorController`, which in the current application is constructed once and never replaced — so the pointer is safe in practice. If a future change allows swapping the active processor model at runtime without reconstructing `SimulatorController`, this pointer would need to either be re-supplied on every access or the widget would need to stop caching it and instead request memory fresh each refresh.

## 7. Thread Safety

**GUI-thread only**, as a `QWidget` subclass.

## 8. Inter-Class Interactions

- **Receives from `MainWindow`**: `updateDisplay()` (every cycle, called from `onPipelineStateChanged()` with `SimulatorController::memory()`), `setDarkMode()`.
- Does not use `QSettings` or any other global/shared state directly.
