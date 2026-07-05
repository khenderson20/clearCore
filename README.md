<p align="center">
  <img src="assets/clearcore_social_preview.png" alt="clearCore" width="100%"/>
</p>

<h1 align="center">clearCore</h1>

<p align="center">
  Write MIPS assembly, watch it flow through a 5-stage pipeline cycle by cycle,<br>
  and attach real GDB to the running emulator — in your terminal or a Qt6 desktop GUI.<br>
  <b>MIPS today, RISC-V next</b> — on one shared, ISA-agnostic core.
</p>

<p align="center">
  <a href="https://github.com/khenderson20/clearCore/actions/workflows/ci.yml">
    <img src="https://github.com/khenderson20/clearCore/actions/workflows/ci.yml/badge.svg" alt="CI">
  </a>
  <a href="https://codecov.io/gh/khenderson20/clearCore">
    <img src="https://codecov.io/gh/khenderson20/clearCore/graph/badge.svg" alt="codecov">
  </a>
  <a href="https://www.bestpractices.dev/projects/13466">
    <img src="https://www.bestpractices.dev/projects/13466/badge" alt="OpenSSF Best Practices">
  </a>
  <a href="https://scorecard.dev/viewer/?uri=github.com/khenderson20/clearCore">
    <img src="https://api.scorecard.dev/projects/github.com/khenderson20/clearCore/badge" alt="OpenSSF Scorecard">
  </a>
  <a href="https://github.com/khenderson20/clearCore/releases/latest">
    <img src="https://img.shields.io/github/v/release/khenderson20/clearCore?style=flat-square&label=release&color=268BD2" alt="Latest Release">
  </a>
  <a href="https://doi.org/10.5281/zenodo.21194876">
    <img src="https://zenodo.org/badge/1282874868.svg" alt="DOI">
  </a>
  <img src="https://img.shields.io/badge/C%2B%2B-20-00599C?style=flat-square&logo=cplusplus&logoColor=white" alt="C++20">
  <img src="https://img.shields.io/badge/license-MIT-268BD2?style=flat-square" alt="MIT">
</p>

<p align="center">
  <img src="assets/demo_small.gif" alt="clearCore demo" width="85%">
</p>

---

## What is clearCore?

clearCore is a C++20 CPU-architecture simulator built for *seeing* how a processor works. Today it speaks MIPS:
type assembly into the built-in editor, step the CPU, and watch every instruction move through IF/ID/EX/MEM/WB —
stalls, flushes, and forwarding paths highlighted as they happen. When you outgrow hand-typed programs, load a real
`mipsel` ELF binary and debug it with actual GDB through the built-in remote stub.

Two execution engines — a single-cycle datapath and a 5-stage pipeline — implement the same processor interface
and swap at runtime, no rebuild required (the pluggable-backend pattern from Ripes/DrMIPS). Three front ends — a
keyboard-driven terminal UI, a Qt6 Widgets GUI, and a Qt Quick/QML GUI — drive the same core libraries, so pipeline
behavior is identical everywhere. The design follows Harris & Harris (*Digital Design and Computer Architecture*)
and Patterson & Hennessy (*Computer Organization and Design*).

**Why the name — and why not just MIPS.** MIPS originally stood for *Microprocessor without Interlocked Pipeline
Stages*: the hardware dropped the interlocks and left the compiler to schedule around hazards. clearCore puts them
back and lights them up, so you can watch every stall, forward, and flush the real silicon hid. And it isn't
MIPS-only by design — the simulation core was recently split into an ISA-agnostic `isa::` layer (shared memory,
register file, pipeline state, and processor interface), so a **RISC-V (RV32I) backend is next**, reusing all three
front ends and every visualizer unchanged.

*(clearCore began life as a live number-system converter — that converter is still the first tab of the terminal UI.)*

## Download

Prebuilt binaries are attached to every [release](https://github.com/khenderson20/clearCore/releases/latest):

| Platform | Asset | Notes |
|----------|-------|-------|
| **Windows** | `clearCore-<ver>-windows-x64.exe` | NSIS installer, Qt bundled |
| **macOS** (Intel + Apple Silicon) | `clearCore-<ver>-macOS-universal.dmg` | Universal binary, Qt bundled |
| **Linux** | `clearCore-<ver>-Linux-x86_64.tar.gz`, `.AppImage`, `.deb`/`.rpm` | AppImage is self-contained |

> **These builds are unsigned**, so the OS will warn on first launch — this is expected, not a problem with the download:
> - **macOS**: right-click the app → **Open** → **Open** (clears Gatekeeper once). Or *System Settings → Privacy & Security → Open Anyway*.
> - **Windows**: on the SmartScreen prompt, click **More info** → **Run anyway**.

Prefer to build it yourself? Read on.

## Quick Start

You need a **C++20 compiler** (GCC 13+ / Clang 16+) and **CMake 3.25+**. The terminal UI's FTXUI library is fetched
automatically. For the desktop GUIs, install Qt6 (`qt6-qtbase-devel` on Fedora, `qt6-base-dev` on Ubuntu,
`qt@6` via Homebrew) — or skip Qt entirely with the `core-only` preset.

```bash
cmake --preset debug
cmake --build --preset debug

./cmake-build-debug/clearCore-gui              # Qt6 Widgets desktop GUI
./cmake-build-debug/number_system_converter   # terminal UI (needs an ANSI terminal)
./cmake-build-debug/clearCore-quick            # Qt Quick / QML desktop GUI
```

| Preset      | What it builds                                                      |
|-------------|----------------------------------------------------------------------|
| `debug`     | Debug symbols, TUI + Widgets GUI + QML GUI                          |
| `release`   | Optimized release build                                             |
| `asan`      | AddressSanitizer + UBSanitizer (requires `libasan`/`libubsan`)      |
| `core-only` | Simulator core + TUI only — skips Qt6 (both GUIs) and LLVM entirely |

### Your first program

Launch `clearCore-gui`, open the **Code Editor** tab, and load one of the built-in examples — or paste this:

```asm
# Each instruction needs the result of the one before it.
addi $t0, $zero, 1
add  $t1, $t0, $t0
add  $t2, $t1, $t0
add  $t3, $t2, $t1
```

Assemble it, switch to the **Datapath** tab, and step. You'll see the data hazards resolved live: the EX/MEM → EX
forwarding paths light up each cycle as results are bypassed back before they ever reach the register file.

### Tests

```bash
ctest --preset debug    # all suites
ctest --preset asan     # same suites under ASan + UBSan
```

Six CTest suites cover the decoder, disassembler, loader, both CPU backends, and the converter core; when LLVM 15–20
is present, a differential suite validates the disassembler against LLVM's assembler. CI runs Debug and ASan/UBSan
builds plus CodeQL, dependency review, and libFuzzer fuzzing on every PR — see
[Contributing](https://github.com/khenderson20/clearCore/wiki/Contributing).

## Interfaces

| Interface       | Binary                    | Built with      | Highlights                                                              |
|-----------------|---------------------------|-----------------|--------------------------------------------------------------------------|
| **Desktop GUI** | `clearCore-gui`           | Qt6 Widgets     | Dockable panels, code editor with syntax highlighting, hex memory viewer |
| **QML GUI**     | `clearCore-quick`         | Qt Quick / QML  | Same tabs, lighter dependency footprint (Qt6 Quick only), newer          |
| **Terminal UI** | `number_system_converter` | FTXUI           | Fully keyboard-driven (`Tab` panels, `F10` step), runs over SSH          |

All three show the same pipeline state, driven by the same `mips_core` library. The Widgets GUI is the most
feature-complete; the QML GUI mirrors its tab structure declaratively (comparison in the
[Qt6 GUI wiki page](https://github.com/khenderson20/clearCore/wiki/Qt6-GUI)). To trim the build, turn either off
with `-DBUILD_QT6_UI=OFF` / `-DBUILD_QT6_QUICK_UI=OFF`.

<img src="assets/screenshots/tab-01-datapath.png" alt="Datapath tab showing IF/ID/EX/MEM/WB stages during execution">

<details>
<summary><b>More desktop GUI screenshots</b> — pipeline trace, code editor, memory, registers, statistics</summary>
<br>

**Pipeline Trace** — the classic instruction × cycle grid from Patterson & Hennessy, derived from the execution trace.

<img src="assets/screenshots/tab-04-pipeline.png" alt="Pipeline Trace showing an instruction×cycle grid with color-coded stages">

**Code Editor** — assembles MIPS source with labels, branches, and loops directly into the simulator; no external
toolchain. Optional MIPS syntax highlighting when KSyntaxHighlighting is installed (`kf6-syntax-highlighting-devel`
on Fedora, `libkf6syntaxhighlighting-dev` on Ubuntu).

<img src="assets/screenshots/tab-05-codeEditor.png" alt="Code Editor tab showing a MIPS sum-loop program assembled and loaded">

**Memory** — scrollable hex dump, 16 bytes/row with ASCII column, navigable from any base address.

<img src="assets/screenshots/tab-03-memory.png" alt="Memory tab showing a hex dump with ASCII column">

**Registers** — all 32 MIPS registers with ABI aliases (`$t0`, `$sp`, …), updated every cycle.

<img src="assets/screenshots/tab-02-registers.png" alt="Registers tab showing all 32 MIPS registers with ABI aliases">

**Statistics** — cycles, instructions retired, CPI, and per-category hazard/forwarding/stall/flush counts.

<img src="assets/screenshots/tab-06-stats.png" alt="Statistics tab showing CPI, hazard, and forwarding counters">

</details>

<details>
<summary><b>Terminal UI screenshots</b> — converter, CPU dashboard, Core Pulse</summary>
<br>

**Converter** — live three-way DEC/HEX/BIN conversion with R-format bit breakdown and control signal decode.

<img src="assets/screenshots/02_nsc_converter.png" alt="Converter tab showing DEC/HEX/BIN fields and MIPS bit breakdown">

**CPU Dashboard** — registers, instruction decode, execution trace, memory panel, telemetry bar, step/auto/run controls.

<img src="assets/screenshots/03_tab2.png" alt="CPU Dashboard tab showing registers, pipeline state, instruction decode, and telemetry">

**Core Pulse** — ambient oscilloscope animation with per-stage IF/ID/EX/MEM/WB instruction and signal breakdown.

<img src="assets/screenshots/06_tab5_corepulse.png" alt="Core Pulse tab showing signal monitor waveform and pipeline stage details">

Other tabs: **CPU Config** (swap single-cycle ↔ pipelined at runtime), **Program Loader** (flat `.hex` programs),
and **Utility** (diagnostics). See the [Terminal UI wiki page](https://github.com/khenderson20/clearCore/wiki/Terminal-UI).

</details>

## Beyond the basics

**CP0 and hardware exceptions** — `mips_core` implements Coprocessor 0 per the MIPS32r2 spec (`Status`, `Cause`,
`EPC`, `BadVAddr`), so bad opcodes, unaligned accesses, `SYSCALL`, and `BREAK` raise real exceptions that vector to
`0x8000_0180` instead of halting the simulator — the same behavior as physical MIPS hardware.
→ [CP0 & Exceptions](https://github.com/khenderson20/clearCore/wiki/CP0-Exceptions)

**ELF loader** — `mips::load_elf_file_into_processor()` maps a static little-endian MIPS ELF32 binary into the
processor's address space exactly as a kernel exec would, so programs built with `mipsel-linux-gnu-as` or
`mipsel-linux-musl-gcc -static` run unmodified.
→ [ELF Loader](https://github.com/khenderson20/clearCore/wiki/ELF-Loader) (includes toolchain setup)

**GDB remote stub** — attach real GDB to the running emulator and get breakpoints, single-step, register and memory
inspection, and exception-to-signal mapping (`Bp`→SIGTRAP, `RI`→SIGILL, …):

```bash
mipsel-linux-gnu-gdb hello
(gdb) target remote localhost:1234
(gdb) break _start
(gdb) continue
```

→ [GDB Stub](https://github.com/khenderson20/clearCore/wiki/GDB-Stub) (full command reference; POSIX-only, disable with `-DBUILD_GDB_STUB=OFF`)

## Architecture

![how-it-works.svg](assets/how-it-works.svg)

Two pure-logic core libraries (`mips_core`, `nsc_core` — no UI dependencies, tested independently) sit under three
UI layers. The Qt layers bridge CPU state to their views through a shared `SimulatorController`; the TUI talks to
the same interfaces directly. Module-by-module breakdown, design pillars, and academic references are on the
[Architecture wiki page](https://github.com/khenderson20/clearCore/wiki/Architecture).

### How it compares

|                  | clearCore             | Ripes              | DrMIPS          | EduMIPS64        | QtMips            |
|------------------|-----------------------|--------------------|-----------------|------------------|-------------------|
| **Language**     | C++20                 | C++/Qt             | Java            | Java             | C++/Qt            |
| **UI**           | TUI + Qt6 GUI         | Qt GUI             | Swing GUI       | Swing GUI        | Qt GUI            |
| **ISA**          | MIPS (RISC-V planned) | RISC-V             | MIPS            | MIPS64           | MIPS              |
| **Backends**     | 2 (SC / 5-stage)      | 5+ models          | ~2              | ~1               | ~1                |
| **Pipeline viz** | Stage state + hazards | Datapath schematic | Visual datapath | Registers/memory | Datapath + memory |

## Roadmap

Stages 1–2.6 (converter core → pipelined CPU → Qt6 GUIs → CP0/ELF/GDB) are complete — including the recent split
into an **ISA-agnostic core** that clears the way for a second instruction set. Up next:

- [ ] **RISC-V (RV32I)** — a second ISA backend on the shared `isa::` core: decoder → single-cycle → 5-stage pipeline → ELF → GDB, reusing every existing front end and visualizer
- [ ] **Stage 3** — Two-pass assembler with full symbol table and pseudo-instruction expansion
- [ ] **Stage 4** — Per-stage TUI telemetry and CPI analysis, matching the GUI's Pipeline Trace and Statistics tabs
- [ ] **Stage 5** — Branch prediction and speculative execution

Full breakdown on the [Roadmap wiki page](https://github.com/khenderson20/clearCore/wiki/Roadmap).

## Documentation

| Doc                                                                                    | Audience                                                       |
|-----------------------------------------------------------------------------------------|-----------------------------------------------------------------|
| [Getting Started](https://github.com/khenderson20/clearCore/wiki/Getting-Started)      | Beginners learning MIPS concepts through the TUI visualization |
| [Architecture](https://github.com/khenderson20/clearCore/wiki/Architecture)            | Design patterns, hardware abstractions, and academic grounding |
| [CP0 and Exceptions](https://github.com/khenderson20/clearCore/wiki/CP0-Exceptions)    | MIPS32r2 exception model, CP0 registers, ERET, MFC0/MTC0       |
| [ELF Loader](https://github.com/khenderson20/clearCore/wiki/ELF-Loader)                | Loading compiled mipsel binaries; toolchain setup guide        |
| [GDB Stub](https://github.com/khenderson20/clearCore/wiki/GDB-Stub)                    | GDB RSP server — breakpoints, single-step, exception signals   |
| [Qt6 GUI](https://github.com/khenderson20/clearCore/wiki/Qt6-GUI)                      | How `nsc_qt` and `SimulatorController` are structured          |
| [Contributing](https://github.com/khenderson20/clearCore/wiki/Contributing)            | Branching model, code style, and testing guidelines            |
| [Roadmap](https://github.com/khenderson20/clearCore/wiki/Roadmap)                      | Staged feature plan and reference patterns                     |

## Citation

If you use clearCore in research or teaching, please cite it. Each release is archived on Zenodo with a DOI:

[![DOI](https://zenodo.org/badge/1282874868.svg)](https://doi.org/10.5281/zenodo.21194876)

GitHub's **"Cite this repository"** button (top-right sidebar) generates APA and BibTeX from
[`CITATION.cff`](CITATION.cff). For convenience:

```bibtex
@software{henderson_clearcore,
  author  = {Henderson, Kevin},
  title   = {{clearCore}: An educational {CPU}-architecture simulator with live 5-stage pipeline visualization},
  year    = {2026},
  version = {0.1.0},
  doi     = {10.5281/zenodo.21194876},
  url     = {https://github.com/khenderson20/clearCore}
}
```

> The DOI above is the *concept* DOI — it always resolves to the latest release. Zenodo also mints a
> version-specific DOI for each release if you need to cite an exact version.

## License

MIT — see [LICENSE](LICENSE).
