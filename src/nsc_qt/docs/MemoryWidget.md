# MemoryWidget

## 1. Class Overview

`MemoryWidget` renders a scrollable hex dump of the whole emulated address space for the Memory panel, built on [QHexView](https://github.com/Dax89/QHexView) (MIT). A navigation bar holds a "Jump to 0x0" button and a "Go to address" spin box. Bytes that changed since the previous refresh are highlighted until the next refresh; with a refresh every cycle, that is exactly what the last step stored.

`MainWindow` feeds it the controller's memory with `updateDisplay()` whenever the Memory panel is on screen.

## 2. Project Structure and Dependencies

Constructed once by `MainWindow::setupCentralWidget()` and placed in the bottom dock area.

Qt modules required:
- **Qt Widgets** — `QWidget` (base class), `QSpinBox`, `QLabel`, `QPushButton`, `QHBoxLayout`, `QVBoxLayout`.
- **QHexView** — `QHexView`, `QHexDocument`, `QHexBuffer` (fetched by CMake; see `CLAUDE.md`).

Project-internal types:
- `isa::Memory` (`include/isa/memory.h`) — the memory this widget copies from via `raw()`.
- `nsc::qt::scale::monoFont()` (`nsc_qt/ui_scale.h`) — supplies the font sizes for the hex view and nav bar.

## 3. Class Hierarchy and Role

`MemoryWidget` inherits [`QWidget`](https://doc.qt.io/qt-6/qwidget.html) and is `final`. It hosts one `QHexView` whose document is backed by `MemorySnapshotBuffer`, a private `QHexBuffer` subclass that holds the widget's own copy of memory.

## 4. Public Methods

#### `explicit MemoryWidget(QWidget* parent = nullptr)`
Builds the nav bar and the read-only hex view. The document is created on the first `updateDisplay()`, when the memory size is known.

#### `void updateDisplay(const isa::Memory& mem)`
On the first call (or if the size changes), copies the whole image once. After that it compares `mem` with its copy one 4 KiB page at a time (`memcmp`), and walks bytes only inside a page that differs. It copies each changed run into the snapshot, records it (up to 256 runs), highlights it, and repaints. A refresh with no change costs only the page comparisons, and it never resets the view, so the cursor and scroll position survive while the program runs.

#### `void setDarkMode(bool dark)`
Re-colours the header text and the current highlights. It does not touch the simulator's memory.

#### `const std::vector<std::pair<qint64, qint64>>& changedRanges() const noexcept`
The `{offset, length}` byte ranges highlighted by the last refresh. For tests.

## 5. Protected / Private Slots

#### `void onAddressChanged(int value)` *(private slot)*
Connected to the address spin box. Moves the hex cursor to `value`.

## 6. Ownership and Lifecycle

`MemoryWidget` is deleted with the widget tree. The `QHexDocument` is a child of the widget and owns the `MemorySnapshotBuffer`.

The widget keeps no reference to the simulator's memory between calls: every refresh receives it as an argument, and everything the view paints comes from the widget's own snapshot. (An earlier version cached a raw `const mips::Memory*` and dereferenced it later from `setDarkMode()`; see #245.)

## 7. Thread Safety

**GUI-thread only**, as a `QWidget` subclass.

## 8. Inter-Class Interactions

- **Receives from `MainWindow`**: `updateDisplay()` with `SimulatorController::memory()` — every cycle while the Memory panel is visible, and once when it becomes visible again — and `setDarkMode()`.
- Does not use `QSettings` or any other global/shared state.
