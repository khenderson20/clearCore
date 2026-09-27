# SimulatorController

## 1. Class Overview

`SimulatorController` is the bridge between the UI-agnostic `mips_core` library and the Qt6 GUI. It owns a single `mips::IProcessor` instance and re-exposes every state change (cycle execution, pipeline state, statistics, halt/fault conditions, breakpoint hits) as Qt signals, so widgets never talk to the processor directly. `MainWindow` constructs exactly one `SimulatorController` for the lifetime of the application and every tab widget's data ultimately traces back to it.

A developer reaches for this class whenever a widget needs to read simulator state or trigger a step/run/reset — never by calling into `mips::IProcessor` directly.

## 2. Project Structure and Dependencies

Constructed once, by `MainWindow`, wrapping a `mips::PipelinedCpu`.

Qt modules required:
- **Qt Core** — `QObject` (base class), `QMutex`/`QMutexLocker` (internal synchronization), `QTimer` (drives `run()` mode).

Project-internal types:
- `mips::IProcessor` (`mips_core`) — the abstract processor interface this class owns and drives; `SimulatorController` never depends on a concrete processor type beyond the constructor parameter.
- `mips::PipelineState`, `mips::Memory`, `mips::RegisterFile` — read-only state types returned by the accessor methods below.
- `SimulatorStatistics` — a plain struct (declared in the same header) accumulated internally and emitted via `statisticsUpdated`.

Build requirement: compiled into both the `nsc_qt_ui` object library and the `clearCore-gui` executable; also linked into the `qt_ui_test` smoke-test target.

## 3. Class Hierarchy and Role

`SimulatorController` inherits [`QObject`](https://doc.qt.io/qt-6/qobject.html) directly (not `QWidget` — it has no visual representation). This gives it the meta-object system needed for `Q_OBJECT`, signals, and slots, plus parent-based ownership, without any UI/paint machinery it doesn't need.

## 4. Public Member Variables

`SimulatorStatistics` (declared alongside the class, not a member of it) is a plain aggregate returned by value from `statistics()` and carried by the `statisticsUpdated` signal:

| Variable | Type | Description |
|----------|------|-------------|
| `cycles_executed` | `uint64_t` | Total simulator cycles executed since the last `reset()`. |
| `instructions_retired` | `uint64_t` | Instructions that completed WB without being stalled or flushed. |
| `data_hazards` | `uint64_t` | Count of load-use stalls detected. |
| `control_hazards` | `uint64_t` | Count of taken branches/jumps that caused a pipeline flush. |
| `forwarding_events` | `uint64_t` | Count of cycles where EX/MEM→EX or MEM/WB→EX forwarding fired. |
| `stalls` | `uint64_t` | Total stall cycles (mirrors `data_hazards` in the current implementation). |
| `flushes` | `uint64_t` | Total flush cycles (mirrors `control_hazards`). |

`SimulatorStatistics::cpi() const noexcept` returns `cycles_executed / instructions_retired` as a `double`, or `0.0` if no instructions have retired yet — callers should treat `0.0` as "no data" rather than a real CPI of zero.

## 5. Signals

#### `cycleExecuted(uint64_t count)`
Emitted after every `stepCycle()` (whether called directly or via the run timer), carrying the processor's new cycle count. Connected slots typically update a "Cycles: N" label.

#### `pipelineStateChanged(mips::PipelineState state)`
Emitted alongside `cycleExecuted`, carrying a full snapshot of all five pipeline stages. This is the primary signal that drives `DatapathWidget`, `PipelineTraceWidget`, `RegisterWidget`, and `MemoryWidget` updates each cycle.

#### `breakpointHit(uint32_t pc)`
Emitted from `doStep()` when the next PC after a step matches an address in `breakpoints_`, immediately after the run timer has been stopped. A connected slot should update UI state (e.g. re-label a Run/Pause action back to "Run") and typically shows the address to the user.

#### `statisticsUpdated(nsc::qt::SimulatorStatistics stats)`
Emitted alongside `cycleExecuted`/`pipelineStateChanged` on every step, carrying the accumulated `SimulatorStatistics` snapshot for that cycle.

#### `programLoaded(int instructionCount)`
Emitted from `loadProgram()` on success, after `reset()`-equivalent state clearing but before any cycles have executed, carrying the number of instruction words loaded.

#### `halted()`
Emitted from `doStep()` when the processor reports `mips::StepResult::Halt` (spin-loop detection). The run timer is stopped before this signal fires.

#### `faulted()`
Emitted from `doStep()` when the processor reports `mips::StepResult::Fault` (an invalid or unsupported instruction execution). The run timer is stopped before this signal fires.

## 6. Public Methods

#### `explicit SimulatorController(std::unique_ptr<mips::IProcessor> processor, QObject* parent = nullptr)`
Takes ownership of `processor` and constructs an internal `run_timer_` (interval 0, i.e. as fast as the event loop allows) connected to the private `onRunTimer()` slot. Does not start the timer or execute any cycles.

#### `bool loadProgram(const std::vector<uint32_t>& words, uint32_t addr = 0)`
Stops the run timer, loads `words` into the processor's memory starting at `addr`, and resets the accumulated statistics. Returns `false` (without touching state) if the program is too large for memory. On success, emits `programLoaded` followed by `pipelineStateChanged`.

#### `void reset()`
Stops the run timer, calls `mips::IProcessor::reset()`, and zeroes the accumulated statistics. Emits `pipelineStateChanged` and `statisticsUpdated` with the freshly reset state.

#### `void stepCycle()`
Executes exactly one processor cycle via the internal `doStep()` and emits the corresponding signals. Safe to call whether or not the run timer is active — it does not itself touch `run_timer_`.

#### `void run()`
Starts the run timer, causing `onRunTimer()` (and therefore `doStep()`) to fire repeatedly at the interval set by `setExecutionSpeed()`.

#### `void stop()`
Stops the run timer. Idempotent — safe to call even if the timer isn't running.

#### `bool isRunning() const noexcept`
Returns whether the run timer is currently active.

#### `uint64_t cycleCount() const noexcept`
Returns the processor's current cycle count under `mutex_`.

#### `uint32_t registerValue(uint8_t idx) const noexcept`
Returns the value of register `idx` (0–31) under `mutex_`.

#### `std::optional<uint32_t> memoryWord(uint32_t addr) const noexcept`
Returns the 32-bit word at `addr`, or `std::nullopt` if `addr` is out of range, under `mutex_`.

#### `mips::PipelineState pipelineState() const noexcept`
Returns a snapshot of all five pipeline stages under `mutex_`.

#### `SimulatorStatistics statistics() const noexcept`
Returns a copy of the accumulated statistics under `mutex_`.

#### `const mips::Memory& memory() const noexcept`
Returns a reference to the processor's memory. **Not** guarded by `mutex_` — see [Thread Safety](#8-thread-safety).

#### `const mips::RegisterFile& registers() const noexcept`
Returns a reference to the processor's register file. **Not** guarded by `mutex_` — see [Thread Safety](#8-thread-safety).

#### `void setBreakpoint(uint32_t pc)` / `void clearBreakpoint(uint32_t pc)`
Add or remove `pc` from the breakpoint set, under `mutex_`.

#### `bool hasBreakpoint(uint32_t pc) const noexcept`
Returns whether `pc` is currently a breakpoint, under `mutex_`.

#### `const std::unordered_set<uint32_t>& breakpoints() const noexcept`
Returns a reference to the full breakpoint set. **Not** guarded by `mutex_` — see [Thread Safety](#8-thread-safety).

#### `void setExecutionSpeed(int speed)`
Sets the run timer's interval from a 0–100 speed value (clamped): 100 maps to a 0ms interval (as fast as the event loop allows), 0 maps to 500ms per cycle. Does not start or stop the timer.

## 7. Protected / Private Slots

#### `void onRunTimer()` *(private slot)*
Connected to `run_timer_`'s `timeout()` signal; simply calls `doStep()` on every tick.

## 8. Thread Safety

**Single-threaded in practice, despite the internal `QMutex`.** Every call into `SimulatorController` in this application originates from the GUI thread — `stepCycle()`/`run()`/`stop()` are invoked directly from `MainWindow` slots, and repeated stepping during `run()` mode is driven by `run_timer_`, whose `timeout()` signal is also delivered on the GUI thread since the object was constructed there and never moved via `moveToThread()`. Nothing in this codebase constructs a `QThread` or calls `moveToThread()` on a `SimulatorController`.

The `mutable QMutex mutex_` guards `stepCycle()`/`reset()`/`loadProgram()` and most of the `const` accessors, but **not** `memory()`, `registers()`, or `breakpoints()`, which return direct references without locking — an inconsistency that is currently harmless only because there is no second thread to race with. If this class is ever moved to a background thread, those three accessors need to either take the lock or be changed to return snapshots by value like `pipelineState()` and `statistics()` already do.

## 9. Inter-Class Interactions

- **Emits to `MainWindow`**: all seven signals above are connected to `MainWindow` private slots (see `MainWindow.md`), which fan the resulting state out to the tab widgets.
- **Owns**: one `mips::IProcessor` (concretely a `mips::PipelinedCpu`, constructed by `MainWindow`) and one internal `QTimer`.
- **No global/shared state** — does not use `QSettings` or any singleton; all state lives in the instance itself.
