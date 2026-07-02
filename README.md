<div style="text-align: center;">
  <img src="assets/CPU-SIMULATOR.png" alt="ClearCore logo" width="2048"/>
</div>

<div style="text-align: center;">

**A pure C++20 lab for computer architecture: from number bases to MIPS hardware, with both a terminal UI and a Qt6 desktop GUI.**

</div>

<div style="text-align: center;">

![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?style=flat-square&logo=cplusplus&logoColor=white)
![FTXUI](https://img.shields.io/badge/TUI-FTXUI%20v7.0.0-2AA198?style=flat-square)
![Qt6](https://img.shields.io/badge/GUI-Qt6-41CD52?style=flat-square&logo=qt&logoColor=white)
![Tests](https://img.shields.io/badge/tests-95%2F95%20passing-859900?style=flat-square)
![MIT License](https://img.shields.io/badge/license-MIT-268BD2?style=flat-square)
![Build](https://img.shields.io/badge/build-CMake%20%2B%20FetchContent-657B83?style=flat-square)
![Linux](https://img.shields.io/badge/platform-Linux-073642?style=flat-square)

</div>

---

This project started as a live terminal number system converter (binary, hex, decimal) and evolved through deliberate stages into a **pluggable MIPS CPU emulator** with two interchangeable front ends: the original **FTXUI terminal UI** and a newer **Qt6 desktop GUI**. Both sit on top of the same core — interchangeable single-cycle and 5-stage pipelined processor models behind a single `IProcessor` interface — so a cycle-accurate pipeline visualizer, hazard/forwarding badges, and telemetry all behave identically regardless of which front end you're running.

Built on [FTXUI](https://github.com/ArthurSonzogni/FTXUI) and [Qt6](https://www.qt.io/), the architecture follows the **Ripes/DrMIPS pattern**: the abstract `IProcessor` interface lets `SingleCycleCpu` and `PipelinedCpu` implementations swap in and out without touching either UI layer. Both expose the same `PipelineState`, so IF, ID, EX, MEM, and WB render identically across the TUI and the GUI.

## 🖼️ Qt6 desktop GUI

The GUI is the fuller-featured way to work with clearCore: a resizable window with a real code editor, a hex memory viewer, and a click-through pipeline datapath, alongside everything the terminal UI offers.

<table>
<tr>
<td width="50%">

**Datapath**
<img src="assets/screenshots/tab-01-datapath.png" alt="Datapath tab showing the 5-stage pipeline">

</td>
<td width="50%">

**Registers**
<img src="assets/screenshots/tab-02-registers.png" alt="Registers tab showing all 32 MIPS registers">

</td>
</tr>
<tr>
<td width="50%">

**Memory**
<img src="assets/screenshots/tab-03-memory.png" alt="Memory tab showing a hex dump with ASCII column">

</td>
<td width="50%">

**Pipeline Trace**
<img src="assets/screenshots/tab-04-pipeline.png" alt="Pipeline trace tab showing an instruction x cycle grid">

</td>
</tr>
<tr>
<td width="50%">

**Code Editor**
<img src="assets/screenshots/tab-05-codeEditor.png" alt="Code editor tab with an assembled and loaded MIPS program">

</td>
<td width="50%">

**Statistics**
<img src="assets/screenshots/tab-06-stats.png" alt="Statistics tab showing cycles, CPI, hazards, and forwarding counts">

</td>
</tr>
</table>

**What each tab does:**

| Tab                | Purpose                                                                                                                                                                                                |
|--------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| **Datapath**       | The 5-stage pipeline (IF/ID/EX/MEM/WB) rendered live. Double-click a stage for a full decode of the instruction sitting in it; right-click to set or clear a breakpoint on that instruction's address. |
| **Registers**      | All 32 registers with ABI aliases (`$t0`, `$sp`, …), updated every cycle.                                                                                                                              |
| **Memory**         | A scrollable hex dump (16 bytes/row with an ASCII column) from any base address you enter.                                                                                                             |
| **Pipeline Trace** | An instruction × cycle grid — the classic pipeline diagram from Patterson & Hennessy, generated automatically from your program's actual execution instead of drawn by hand.                           |
| **Code Editor**    | Write MIPS assembly directly, or load one of the built-in example programs, then Assemble and Load it into the running simulator.                                                                      |
| **Statistics**     | Cycles, instructions retired, CPI, and per-category hazard/forwarding/stall/flush counts.                                                                                                              |

The assembler supports the same instruction subset as the rest of clearCore (`add`, `addi`, `lw`/`sw`, `beq`/`bne`, `j`/`jal`, shifts, and friends) plus labels, so branches and loops assemble the same way they would on real MIPS toolchains.

### Building the GUI

The Qt6 GUI is built by default alongside the terminal UI:

```bash
cmake -S . -B cmake-build-debug
cmake --build cmake-build-debug --target clearCore-gui
./cmake-build-debug/clearCore-gui
```

> ⚠️ **Qt6 is required by default.** `BUILD_QT6_UI` defaults to `ON`, so `cmake --build` will fail at the configure step if Qt6 isn't installed. Either install it, or configure with the GUI turned off:
>
> ```bash
> # Install Qt6 (pick your platform)
> sudo dnf install qt6-qtbase-devel qt6-qtbase-gui   # Fedora/RHEL
> sudo apt install qt6-base-dev                      # Ubuntu/Debian
> brew install qt@6                                   # macOS
>
> # ...or skip the GUI entirely and build the TUI only
> cmake -S . -B cmake-build-debug -DBUILD_QT6_UI=OFF
> ```

## ✨ Key Features

- **Live Number Conversion** — instant two-way conversion between binary, hex, and decimal, backed by a single `uint64_t` source of truth with robust input validation.
- **CPU Mode Switching** — toggle between single-cycle and 5-stage pipelined CPUs at runtime, no rebuild required.
- **Pipeline Visualization** — all five stages rendered cycle by cycle, with color-coded forwarding paths (EX→EX, WB→EX) and hazard badges (load-use stall, branch/jump flush) — in both the TUI and the Qt6 datapath view.
- **MIPS Instruction Decoding** — enter a raw 32-bit value and see its mnemonic, register fields, and binary breakdown decoded live.
- **In-app assembler** — write or load MIPS assembly with labels, branches, and loops directly in the Qt6 Code Editor tab, no external toolchain needed.
- **Telemetry & CPI** — running cycle counters, stall/forward/flush tallies, and a live CPI readout, shared by both interfaces.
- **Breakpoints & stage inspection** — set breakpoints from the Qt6 Datapath tab and step through a run instruction by instruction.
- **Signal Monitor** *(TUI only)* — an ambient oscilloscope panel that animates while the CPU runs.
- **Control & Navigation** — the TUI is fully keyboard-driven (`Tab` to move between panels, `F10` to step, `Esc` to quit); the Qt6 GUI supports both mouse and keyboard, including arrow-key navigation and Enter/Space shortcuts on the Datapath tab.

## 🖥️ Interfaces

clearCore ships two independent front ends over the same `mips_core`/`nsc_core` libraries. Pick whichever fits your terminal — or build both.

### Terminal UI (`number_system_converter`)

| # | Tab                | Purpose                                              |
|---|--------------------|------------------------------------------------------|
| 0 | **Converter**      | Live binary/hex/decimal conversion                   |
| 1 | **CPU Dashboard**  | Registers, pipeline stages, hazard badges, telemetry |
| 2 | **CPU Config**     | Switch between single-cycle and pipelined backends   |
| 3 | **Program Loader** | Load a flat instruction-word program into memory     |
| 4 | **Signal Monitor** | Ambient oscilloscope animation during execution      |

### Qt6 desktop GUI (`clearCore-gui`)

See the [screenshot gallery and tab reference](#️-qt6-desktop-gui) above.

## 🚀 Quick Start

### Build

The build is fully self-contained via CMake FetchContent (FTXUI v7.0.0) — no system-wide FTXUI install required. Qt6 is a separate system dependency (see [Building the GUI](#building-the-gui) above); if you don't want to install it, configure with `-DBUILD_QT6_UI=OFF` first.

```bash
cmake -S . -B cmake-build-debug
cmake --build cmake-build-debug --target number_system_converter
```

### Run

> ⚠️ **Important:** FTXUI requires a real terminal environment to render correctly due to ANSI escape codes. If running from an IDE, enable "Emulate terminal in output console" or run directly from the shell.

```bash
./cmake-build-debug/number_system_converter
```

Start by entering `255` in DEC and watch HEX (`FF`) and BIN (`11111111`) update live.

Prefer the desktop GUI? Build and run `clearCore-gui` instead — see [Building the GUI](#building-the-gui).

### Testing

95/95 checks passing across the decoder, both CPU backends, and the converter core. The Qt6 GUI has its own smoke-test suite (`qt_ui_test`), built only when `BUILD_QT6_UI` is on.

```bash
cmake --build cmake-build-debug --target decoder_test cpu_test processor_test nsc_tests qt_ui_test
ctest --test-dir cmake-build-debug --output-on-failure
```

## 🧠 Technical Architecture

The system is split into two decoupled core libraries plus two independent UI layers, all wired together with CMake:

- **`nsc_core`** — the number system converter logic.
- **`mips_core`** — the processor logic, behind the `IProcessor` interface.
- **`nsc_ui`** — FTXUI wiring for the terminal UI; never includes core libraries directly, only their public interfaces.
- **`nsc_qt`** — Qt6 widgets for the desktop GUI; likewise only depends on `mips_core` through `IProcessor`, via a `SimulatorController` bridge that owns the processor and re-emits its state as Qt signals for the widgets to render.

### MIPS Core: Pluggable Processors (`mips/`)
`IProcessor` is the contract between either UI and any execution engine, so both backends are interchangeable and both front ends drive them identically:

- **`SingleCycleCpu`** — a basic, non-pipelined datapath (H&H Chapter 7).
- **`PipelinedCpu`** — a full 5-stage pipeline (IF, ID, EX, MEM, WB) adhering to H&H Chapter 8, including:
  - Load-use stall detection
  - Forwarding paths (EX/MEM → EX and MEM/WB → EX)
  - Hazard detection and control-flow resolution (branch/jump flushes)

### Core Module Responsibilities

| Module                  | Responsibility                                                                  | NSC Core | MIPS Core | Qt GUI |
|:------------------------|:--------------------------------------------------------------------------------|:--------:|:---------:|:------:|
| **Converter**           | Manages `uint64_t` state, exposes base views                                    |    ✅     |           |        |
| **Parser/Formatter**    | String validation and serialization across bases                                |    ✅     |           |        |
| **IProcessor**          | Abstract interface for execution engine + visualizer contract                   |          |     ✅     |        |
| **CPUs (SC/Pipe)**      | Core CPU implementation logic (datapath)                                        |          |     ✅     |        |
| **Decoder / ALU**       | Instruction format detection, control signals, arithmetic/logic                 |          |     ✅     |        |
| **SimulatorController** | Owns an `IProcessor`, re-emits its state as Qt signals for widgets to render    |          |           |   ✅    |
| **In-app assembler**    | Parses MIPS assembly with labels into instruction words for the Code Editor tab |          |           |   ✅    |

### Design Conventions & Built With
- **Languages/Tools:** C++20 (`std::format`, `std::optional`), FTXUI v7.0.0, Qt6 (Widgets, OpenGL), CMake FetchContent.
- **Design Focus:** `mips_core` and `nsc_core` contain pure logic with no UI dependency; polymorphism via `IProcessor` keeps backends swappable; `enum class` for hardware fields; `[[nodiscard]]` on pure queries.

## 🌍 Ecosystem Positioning

ClearCore sits alongside established educational and research simulators, applying the Ripes pluggable-backend pattern — and, uniquely among this set, offering both a terminal UI and a native desktop GUI over the same core.

| Aspect            | ClearCore                   | Ripes              | DrMIPS          | EduMIPS64       | QtMips            | WebRISC-V   |
|-------------------|-----------------------------|--------------------|-----------------|-----------------|-------------------|-------------|
| **Language**      | C++20                       | C++/Qt             | Java            | Java            | C++/Qt            | PHP/JS      |
| **UI**            | FTXUI (TUI) **+** Qt6 (GUI) | Qt (GUI)           | Swing (GUI)     | Swing (GUI)     | Qt (GUI)          | Web Browser |
| **ISA**           | MIPS                        | RISC-V             | MIPS            | MIPS64          | MIPS              | RISC-V      |
| **Backends**      | 2 (SC / 5-stage)            | 5+ models          | ~2              | ~1              | ~1                | ~1          |
| **Visualization** | Pipeline state + hazards    | Datapath schematic | Visual datapath | Register/memory | Datapath + memory | Cycle grid  |

Reference texts: **Harris & Harris**, *Digital Design and Computer Architecture* (single-cycle datapath, control signal generation), and **Patterson & Hennessy**, *Computer Organization and Design* (pipelining, hazards, forwarding).

## 🗺️ Roadmap

- ✅ **Stage 1** — Number converter core + MIPS decoder
- ✅ **Stage 1.5** — `IProcessor` refactor, single-cycle and pipelined backends
- 🟡 **Stage 2** — TUI execution visualizer (memory panel, instruction decode, hazard badges, speed controls, telemetry — complete)
- ✅ **Stage 2.5** — Qt6 desktop GUI (datapath, registers, memory, pipeline trace, code editor with in-app assembler, statistics)
- ⬜ **Stage 3** — Two-pass assembler with symbol table, label resolution, pseudo-instructions *(the Qt6 Code Editor's assembler covers a first pass of this already — labels and branches work; pseudo-instructions are still outstanding)*
- ⬜ **Stage 4** — Instruction × cycle grid, per-stage telemetry, CPI analysis, performance summary panel *(the Qt6 Pipeline Trace and Statistics tabs cover this for the GUI; TUI-side work is still outstanding)*
- ⬜ **Stage 5** — Branch prediction and speculative execution

See [docs/ROADMAP.md](docs/ROADMAP.md) for the full breakdown.

## 📄 Documentation

- **🚀 For Beginners:** [USER_GUIDE.md](docs/USER_GUIDE.md) — learn MIPS concepts through the TUI visualization.
- **🧠 For Developers:** [ARCHITECTURE_DESIGN.md](docs/ARCHITECTURE_DESIGN.md) — design patterns, hardware abstractions, and academic grounding.
- **🖼️ Qt6 GUI Architecture:** [QT6_ARCHITECTURE.md](docs/QT6_ARCHITECTURE.md) — how `nsc_qt` and `SimulatorController` are structured.
- **⚙️ For Contributors:** [CONTRIBUTING.md](docs/CONTRIBUTING.md) — branching model, code style, and testing guidelines.
- **🗺️ Roadmap:** [ROADMAP.md](docs/ROADMAP.md) — staged feature plan and reference patterns.

## 📄 License

MIT — see [LICENSE](LICENSE).