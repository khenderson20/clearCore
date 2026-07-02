# Qt6 GUI

The desktop interface (`clearCore-gui`) is built with Qt6 Widgets, plus two vendored UI libraries fetched via CMake: [Qt-Advanced-Docking-System](https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System) for the dockable panel layout and [QHexView](https://github.com/Dax89/QHexView) for the Memory tab's hex viewer. It shares no UI code with the FTXUI terminal UI — both compile from the same `mips_core` and `nsc_core` libraries.

A second, parallel desktop interface — `clearCore-quick`, built on Qt Quick/QML — also exists; see [QML (nsc_quick)](#qml-nsc_quick) below.

---

## Launch

```bash
cmake --build cmake-build-debug --target clearCore-gui
./cmake-build-debug/clearCore-gui
```

Qt6 must be installed on the system (see [Getting Started](Getting-Started)). If Qt6 isn't found at configure time, this target is silently skipped and the rest of the build still succeeds.

---

## Six tabbed views

### Datapath

A clickable diagram of the 5-stage MIPS pipeline. Each stage box (IF, ID, EX, MEM, WB) is clickable — clicking (or pressing **Space**/**B** on the selected stage) highlights that stage and shows its current pipeline register contents in a side panel. Right-click a stage to set or clear a **breakpoint** on its PC; execution pauses and the status bar reports the hit address.

Color coding follows the same convention as the TUI: green for normal advance, yellow for stall bubbles, red for flush, cyan when a forwarding path is active.

### Registers

All 32 MIPS registers in a scrollable table: register number, ABI name (`$zero`, `$at`, `$v0`, …, `$ra`), hex value, and signed-decimal value. The row for the most recently written register is highlighted in the same color as the Datapath's WB stage.

### Memory

A scrollable hex dump (QHexView-backed) of the address space, 16 bytes/row with an ASCII column, navigable from any base address. The row containing the current PC is highlighted.

### Pipeline Trace

An instruction × cycle grid (the classic Patterson & Hennessy layout, also seen in WebRISC-V): rows are instructions in program order, columns are clock cycles, and each cell shows which pipeline stage that instruction occupied during that cycle. Stall bubbles and flush events are color-coded. This view accumulates during a run and resets when the CPU resets.

### Code Editor

An inline MIPS assembler. Write assembly text in the left pane; the right pane shows the assembled instruction words. Features:

- Label definitions (`loop:`) and label references in branch/jump targets
- Forward and backward branch resolution
- Line-numbered syntax error reporting
- **Assemble & Load** — assembles and transfers the program to the CPU in one click

The supported instruction set matches the decoder's ISA subset (see [MIPS CPU Simulator](MIPS-CPU-Emulator#supported-isa-subset)). Pseudo-instructions (`li`, `move`, `la`) and assembler directives (`.data`/`.text`/`.word`) are **not** supported yet — that's the remaining Stage 3 scope; see [Roadmap](Roadmap).

### Statistics

A post-run summary dashboard: total cycles, committed instructions, CPI, and stall / forward / flush counts, with a proportional hazard-type breakdown bar.

---

## Architecture: `SimulatorController`

The CPU does not run on the main Qt thread. The `SimulatorController` class mediates:

```
CPU thread                          Main thread (UI)
─────────────────                   ─────────────────
PipelinedCpu::step()
  → emit pipelineStateChanged()  ──→  Datapath tab updates
  → emit registersChanged()      ──→  Registers tab updates
  → emit memoryChanged()         ──→  Memory tab updates
  → emit traceRowAdded()         ──→  Pipeline Trace updates
  → emit statisticsChanged()     ──→  Statistics tab updates
  → emit breakpointHit()         ──→  Status bar message
```

Qt's event loop automatically marshals signals from the CPU thread to the UI thread. No mutexes are needed in widget code. The controller is the only object that touches the `IProcessor` from outside the CPU thread.

### Thread safety rules

- Widget `paintEvent()` and slot handlers run on the main thread — they may read cached copies of CPU state but must never call `IProcessor` methods directly.
- `SimulatorController` slots that call `step()` are invoked so they execute on the CPU thread.
- Signal parameters are passed by value (not reference) to survive thread-boundary copying.

---

## Build notes

CMakeLists.txt compiles the TUI and both GUI targets by default (`BUILD_QT6_UI` and `BUILD_QT6_QUICK_UI` both default `ON`). Building only the Widgets GUI:

```bash
cmake --build cmake-build-debug --target clearCore-gui
```

New `.cpp` or `.h` files added to `nsc_qt/` must be registered in `CMakeLists.txt` — Qt's MOC (meta-object compiler) will not pick them up automatically and you will get a linker error, not a compile error.

---

## QML (`nsc_quick`)

`src/nsc_quick` and `qml/ClearCore` contain a second, Qt Quick/QML-based interface (`clearCore-quick`) that targets the same `mips_core` backend, built by default alongside the Widgets GUI. Its QML components (`Main.qml`, `DatapathStrip.qml`, `RegistersPane.qml`, `MemoryPane.qml`, `TracePane.qml`, `StatsPane.qml`, `EditorPane.qml`, plus shared `Theme.qml`, `CCButton.qml`, `TransportBar.qml`, `StatusPill.qml`, `HazardBadge.qml`, `StageCard.qml`) mirror the Widgets GUI's tab structure. It reuses the same `assembler.h`/`.cpp` and `examples.h`/`.cpp` from `nsc_qt` rather than duplicating them.

```bash
cmake --build cmake-build-debug --target clearCore-quick
./cmake-build-debug/clearCore-quick
```

This interface isn't yet documented in the top-level README — treat it as available but less battle-tested than the Widgets GUI until that's reconciled.
