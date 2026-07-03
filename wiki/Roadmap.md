# Roadmap

The project follows a staged development plan. Each stage builds on the previous without breaking existing functionality. Stage numbering below matches the top-level [README](https://github.com/khenderson20/clearCore/blob/main/README.md#roadmap); this page adds the detail behind each checkbox.

---

## Completed

### Stage 0 — Foundations

- Instruction decoder (R/I/J formats, opcode + funct → mnemonic)
- ALU control block and execute (arithmetic, logic, shifts, overflow flags)
- Live MIPS decode in converter UI
- `RegisterFile` (32 × `uint32_t`, `$zero` hardwired)
- `Memory` (byte-addressable, word/half/byte access, alignment checking)

### Stage 1 — Single-Cycle Datapath

- `SingleCycleCpu`: fetch → decode → control → execute → memory → writeback in one cycle
- Full control unit signals (`RegWrite`, `MemRead`, `MemWrite`, `Branch`, `Jump`, `ALUSrc`, `ALUOp`, `MemToReg`, `RegDst`)
- ISA subset: arithmetic, logic, shifts, loads, stores, branches, jumps (see [MIPS CPU Simulator](MIPS-CPU-Emulator#supported-isa-subset) for the exact instruction list)
- Program loader for flat instruction arrays

### Stage 1.5 — IProcessor Refactor & Pipelined Backend

- Abstract `IProcessor` interface (Ripes/DrMIPS pattern)
- `PipelineState` observable struct (IF/ID/EX/MEM/WB snapshots, forwarding flags, hazard indicators)
- `SingleCycleCpu` refactored to implement `IProcessor`
- `PipelinedCpu`: full 5-stage concurrent pipeline
- Forwarding unit (EX/MEM → EX, MEM/WB → EX)
- Hazard detection (load-use stall, branch/jump flush)
- Polymorphic test harness (`processor_test.cpp`) validating both implementations against the same programs

### Stage 2 — TUI Execution Visualizer

- CPU mode switcher (single-cycle ↔ pipelined at runtime)
- Register file panel with last-written highlight and signed-decimal annotation
- Datapath panel (PC, instruction, cycle count, per-stage control signal snapshot)
- Pipeline strip with inline stall/forward/flush badges
- Memory panel (hex dump, PC-highlighted row, PgUp/PgDn/Home-to-PC navigation)
- Instruction decode panel (per-field color coding, reconstructed assembly, raw binary)
- Execution speed control (step, auto-run, run-to-halt, speed slider 10–1000 ms/cycle)
- Hazard visualization panel (Arches-style: stall/forward/flush inline + dedicated status panel)
- 8-entry execution trace (last committed instructions, WB-stage sourced)
- Telemetry panel (live stalls / forwards / flushes / CPI gauges)
- Oscilloscope startup splash and ambient "Core Pulse" animation
- Fixed `Container::Tab` focus-routing bug (tabs 4–5 aliasing onto 0–1's interactive components)
- Fixed emoji double-width misalignment in tab bar and headers

### Stage 2.5 — Qt6 Desktop GUI

*This stage isn't in the version of this page you may have seen before — it shipped as a full desktop GUI, not a plan.*

- `clearCore-gui`: Qt6 Widgets application with six tabs — Datapath, Registers, Memory, Pipeline Trace, Code Editor, Statistics (see [Qt6 GUI](Qt6-GUI))
- `SimulatorController`: runs the CPU on a background thread, re-emits state as Qt signals
- **In-app MIPS assembler** (`nsc_qt::assemble()`, `include/nsc_qt/assembler.h` / `src/nsc_qt/assembler.cpp`) — labels, forward/backward branch resolution, line-numbered error reporting, feeding the Code Editor tab directly. This covers part of Stage 3's original scope (see below).
- Clickable Datapath diagram with **address breakpoints** — right-click or Space/B to set/clear, execution pauses on hit
- Pipeline Trace tab (instruction × cycle grid)
- Statistics tab (post-run CPI and hazard-type breakdown)
- Built on two additional fetched dependencies: Qt-Advanced-Docking-System (dockable panels) and QHexView (Memory tab)
- A second, parallel **Qt Quick / QML** interface (`clearCore-quick`, `src/nsc_quick`, `qml/ClearCore/`) targeting the same backend, built by default alongside the Widgets GUI — see [Qt6 GUI § QML](Qt6-GUI#qml-nsc_quick)
- Optional **Nyxstone** (LLVM-based) assembler/disassembler bridge in `mips_core`, gated behind `BUILD_NYXSTONE` and LLVM 15+ availability
- **MARS differential ("golden") test suite** (`tests/golden/`) — cross-checks both CPU models against MARS, the classroom-standard reference MIPS simulator, on seven corpus programs

---

## In progress / upcoming

### Stage 3 — Assembler & Program Composition

The Qt6 Code Editor's in-app assembler (above) already covers labels and branch/jump resolution for the existing ISA subset. What's left to reach full Stage 3 scope:

- [ ] Two-pass assembler with a full symbol table (EduMIPS64 pattern) — current assembler is effectively single-pass with backpatched labels
- [ ] Pseudo-instructions: `li`, `move`, `la`, `nop`-as-macro, with expansion rules
- [ ] Directive support: `.word`, `.ascii`, `.data`, `.text`
- [ ] Bring the same assembler to the TUI (currently GUI-only) — likely landing on the Tab 5 "Utility" placeholder
- [ ] Symbol export for inspection in the pipeline visualizers

**Reference:** EduMIPS64 — GNU assembler directives, case-insensitive mnemonics, forward-reference support.

---

### Stage 4 — Advanced Visualizations & Telemetry (TUI/GUI parity)

The Qt6 GUI's Pipeline Trace and Statistics tabs already deliver most of this for the desktop; the TUI is catching up:

- [ ] Instruction × Cycle Grid in the TUI (WebRISC-V pattern) — the Qt6 GUI already has this via Pipeline Trace
- [ ] Performance Summary Panel in the TUI — the Qt6 GUI already has this via Statistics
- [ ] Per-instruction-type hazard breakout (not just per-cycle totals), in both UIs
- [ ] Cycle-by-cycle breakdown panel — annotate each cycle with reason (normal / stall type / flush type)
- [ ] "Squashed Loops" mode (WebRISC-V) — compact view for repetitive loop execution

---

### Stage 5 — Branch Prediction & Speculative Execution

- [ ] 1-bit predictor (always predict not-taken, flip on misprediction)
- [ ] 2-bit saturating counter predictor (Patterson & Hennessy §4.7)
- [ ] Branch Target Buffer (BTB) — associative cache of recent branch targets
- [ ] Return Address Stack — for `JAL`/`JR` call/return prediction
- [ ] Misprediction visualization — flushed instructions labeled "prediction wrong" in pipeline
- [ ] Prediction accuracy metrics (correct/incorrect counts, per-branch breakdown)
- [ ] Pattern analyzer — identify branch correlations and loop-invariant conditions

**Reference:** SimpleScalar — implement at increasing sophistication levels, each selectable for teaching.

---

## Stretch goals

### Core extensibility

- [ ] RISC-V backend (implements `IProcessor`; reuses all existing visualizers)
- [ ] Configurable datapath — load processor config from JSON/YAML to swap components
- [ ] Cache simulator — configurable L1/L2, replacement policies, coherence (EduMIPS64 + Dinero pattern)
- [ ] Extend the ISA subset — `SRAV`, `LB`/`SB`/`LH`/`SH`, `BGTZ`/`BLEZ`/`BGEZ`/`BLTZ` are not yet decoded (see [MIPS CPU Simulator](MIPS-CPU-Emulator#supported-isa-subset))

### System integration

- [ ] Memory-mapped I/O — console output via syscalls
- [ ] Linux ABI (O32) — syscall emulation (`read`, `write`, `exit`) for running real MIPS programs
- [ ] WebAssembly build — port to WASM for browser deployment (Ripes / QtMips pattern)

### Advanced educational

- [x] Address breakpoints (Qt6 GUI Datapath tab — set/clear via right-click or Space/B)
- [ ] Watchpoints — pause on register-value change, not just PC
- [ ] Reverse execution — undo previous cycles for post-mortem analysis (DrMIPS feature)
- [ ] Snapshot/restore — save and reload execution states for comparison

### Regression / quality

- [x] ClusterFuzzLite fuzzing harness (`tests/fuzz/fuzz_hex_loader.cpp`) — libFuzzer targets `mips::parse_hex_program`; runs 120 s on every PR via `cflite_pr.yml` (addresses OpenSSF Scorecard fuzzing signal)
- [ ] UI smoke test guarding `Container::Tab` child count against `tab_labels.size()` (prevents reintroduction of the tab/focus aliasing bug)
- [ ] Regression test for per-instruction-type telemetry breakout

---

## Academic publication path

| Venue                                                  | Target milestone                    |
|----------------------------------------------------------|----------------------------------------|
| ACM WCAE (Workshop on Computer Architecture Education) | Stage 2 completion                       |
| IEEE Transactions on Education                          | Full pipeline + telemetry (Stage 4)      |

Cross-cite with Ripes, WebRISC-V, and Arches in any submission.

---

> **A note on drift:** the previous version of this page said "no `assembler.h` or `assembler.cpp` exists yet" for Stage 3 — that was true when it was written but is no longer accurate; the Qt6 GUI's in-app assembler landed as part of what the README now calls Stage 2.5. This kind of drift is exactly what the wiki-sync process described in [Contributing](Contributing) is meant to catch going forward.
