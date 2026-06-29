# 🔢 Number System Converter → MIPS CPU Emulator

A modern C++20 project that began as a snappy terminal UI for converting numbers between **binary**, **hexadecimal**, and **decimal** — live, as you type — and is now a **pluggable MIPS CPU emulator** with two interchangeable processor models (single-cycle and 5-stage pipelined) and a live datapath visualizer. Built with [FTXUI](https://github.com/ArthurSonzogni/FTXUI).

The architecture follows the **Ripes/DrMIPS pattern**: an abstract `IProcessor` interface lets single-cycle and pipelined implementations slot in without UI changes. Each processor exposes `PipelineState` — observable per-stage snapshots and forwarding paths — so the visualizer can display both implementations identically.

![demo.gif](assets/demo.gif)
## ✨ Features

- **Live two-way conversion** — edit any field and the others update instantly.
- **Input validation** — each field only accepts characters valid for its base.
- **Nibble-grouped bit view** — binary layout grouped into 4-bit chunks.
- **Live MIPS decode** — enter a 32-bit value and see its mnemonic inline.
- **Keyboard-driven** — `Tab` to navigate, `F10` to step, `Esc` to quit.
- **64-bit converter range** — backed by a single `uint64_t` source of truth.
- **CPU implementation switcher** — toggle between single-cycle and pipelined modes without restarting.
- **Pipeline visualizer** — see all 5 stages (IF, ID, EX, MEM, WB) per cycle; forwarding paths and hazard flags displayed.
- **Unified interface** — `IProcessor` abstraction means both CPU models work identically from the UI's perspective.

## 🚀 Quick start

### Build

The build is fully self-contained — CMake fetches FTXUI v7.0.0.

```bash
cmake -S . -B cmake-build-debug
cmake --build cmake-build-debug --target number_system_converter
```

### Run

> ⚠️ **Run it in a real terminal.** FTXUI draws with ANSI escape codes and needs a
> TTY. Inside CLion's *Run* tool window you'll see garbled border fragments — either
> enable **"Emulate terminal in output console"** in the Run Configuration, or just
> run it from a terminal:

```bash
./cmake-build-debug/number_system_converter
```

Type `255` in the DEC field and watch HEX become `FF` and BIN become `11111111`.

### Test

```bash
cmake --build cmake-build-debug --target decoder_test cpu_test processor_test nsc_tests
ctest --test-dir cmake-build-debug --output-on-failure
```

**Expected output:** All tests pass (38 cpu_test + 95 processor_test + more).

## 🧠 Architecture: Pluggable Processor Backends

### `IProcessor` Interface (processor.h)

The core abstraction that decouples UI from execution engine:

```cpp
class IProcessor {
    virtual StepResult step() = 0;
    virtual void reset(bool clear_memory = false) = 0;
    
    // Accessors
    virtual uint32_t pc() const = 0;
    virtual RegisterFile& regs() = 0;
    virtual Memory& mem() = 0;
    virtual std::size_t cycle_count() const = 0;
    
    // Visualizer contract
    virtual const PipelineState& pipeline_state() const = 0;
};
```

**Two implementations:**
- **`SingleCycleCpu`** (src/mips/single_cycle_cpu.cpp) — H&H Chapter 7: fetch → decode → execute → memory → writeback in one `step()`
- **`PipelinedCpu`** (src/mips/pipelined_cpu.cpp) — H&H Chapter 8: 5-stage pipeline with forwarding, hazard detection, and stall/flush handling

**Benefits:**
- UI talks only to `IProcessor&` — works with any implementation
- Tests run through both backends via polymorphic harness (processor_test.cpp)
- Stage 4 (pipeline visualizer) reuses `PipelineState` for both models
- New backends can be added (future: RISC-V, RTL simulator) without UI changes

### Control Unit & Hazard Detection

`derive_control(const DecodedInstr&)` — single implementation, linked by both CPUs — maps opcode + funct to control signals (reg_write, mem_read, mem_write, alu_src, branch, jump, etc.). This prevents duplication of the switch table.

**Pipelined CPU features:**
- **Load-use stall detection** (H&H §8.5): when ID reads a register that EX's load will write, stall for one cycle
- **EX/MEM → EX forwarding** (priority) + **MEM/WB → EX** forwarding (secondary)
- **Branch resolution in EX** (2-cycle flush): BEQ/BNE/JR/JALR
- **Jump resolution in ID** (1-cycle flush): J/JAL
- **Halt propagation** via self-targeting jump (`j self`)

## 📦 Project layout

```
include/
  nsc/
    ├── converter.h     — orchestrates base conversion
    ├── parse.h         — string → uint64_t with validation
    ├── format.h        — uint64_t → string (binary, hex, decimal, nibble-grouped)
    └── ui.h            — TUI entry point
  
  mips/
    ├── processor.h              — IProcessor interface + PipelineState + Control
    ├── pipeline_regs.h          — IfId, IdEx, ExMem, MemWb structs
    ├── single_cycle_cpu.h/.cpp  — SingleCycleCpu implementation
    ├── pipelined_cpu.h/.cpp     — PipelinedCpu implementation (5-stage)
    ├── cpu.h                    — backward-compat shim: using Cpu = SingleCycleCpu
    ├── decoder.h/.cpp           — instruction decode (R/I/J formats)
    ├── alu.h/.cpp               — ALU control + execute (arithmetic, logic, shifts)
    ├── registers.h/.cpp         — 32 × uint32_t register file ($zero hardwired)
    └── memory.h/.cpp            — byte-addressable RAM with word/half/byte access

src/
  nsc/
    ├── main.cpp        — entry point
    ├── ui.cpp          — FTXUI frontend (4 tabs: Converter, CPU Dashboard, Config, Loader)
    ├── converter.cpp   — conversion orchestrator
    ├── parse.cpp       — parsing with error handling
    └── format.cpp      — serialization
  
  mips/
    ├── single_cycle_cpu.cpp   — unicycle datapath
    ├── pipelined_cpu.cpp      — 5-stage pipeline with forwarding & hazards
    ├── decoder.cpp
    ├── alu.cpp
    ├── registers.cpp
    └── memory.cpp

tests/
  nsc/
    └── convert_test.cpp        — unit tests for parsing/formatting/conversion
  mips/
    ├── decoder_test.cpp        — instruction decode tests (all R/I/J formats)
    ├── cpu_test.cpp            — single-cycle + pipelined (via Cpu alias)
    └── processor_test.cpp      — polymorphic test suite (run_on_both)
```

### nsc_core

| Module        | Responsibility |
|---|---|
| **converter** | Manages `uint64_t value` as source of truth; exposes views in all bases |
| **parse**     | `parseBase(str, base)` → `std::optional<uint64_t>` with validation |
| **format**    | Serializers: `toBinary()`, `toHex()`, `toDecimal()`, `groupBits()` |

### mips_core

| Module        | Responsibility |
|---|---|
| **processor** | `IProcessor` interface; `PipelineState` for visualization; `derive_control()` |
| **pipeline_regs** | Inter-stage register structs (IfId, IdEx, ExMem, MemWb) |
| **single_cycle_cpu** | Unicycle implementation; also defines `derive_control()` |
| **pipelined_cpu** | 5-stage pipeline with forwarding, hazard detection, and flush logic |
| **decoder** | Format detection, field extraction, mnemonic generation |
| **alu** | Control block mapping (opcode, funct) → AluOp; execute arithmetic/logic/shifts |
| **registers** | RegisterFile: 32 registers, `$zero` hardwired to 0 |
| **memory** | Byte-addressable RAM; read_word/half/byte and write_word/half/byte with alignment checks |

## 🛠️ Built with

- **C++20** — `std::format`, `std::optional`, `std::variant`, `std::string_view`, designated initializers, `std::unique_ptr`
- **FTXUI v7.0.0** — reactive DOM-based TUI layer (screen, components, dom elements)
- **CMake FetchContent** — declarative dependency management

## 🗺️ Roadmap: MIPS CPU Emulator

References: Harris & Harris, *Digital Design and Computer Architecture* (MIPS edition); Patterson & Hennessy, *Computer Organization and Design*; scholarly works: Ripes (Petersen, 2021), DrMIPS (Nova et al., 2013), WebRISC-V (Giorgi & Mariotti, 2024), PSBE/Pipelined MIPS Simulation, Arches (Haydel et al., 2025).

### ✅ Stage 0 — Foundations

- [x] Instruction decoder (R/I/J formats, opcode + funct → mnemonic)
- [x] ALU control block + execute (arithmetic, logic, shifts, overflow flags)
- [x] Live MIPS decode in converter UI
- [x] RegisterFile (32 × uint32_t, `$zero` hardwired)
- [x] Memory (byte-addressable, word/half/byte access, alignment)

### ✅ Stage 1 — Single-Cycle Datapath

- [x] CPU: fetch → decode → control → execute → memory → writeback in one cycle
- [x] Control unit signals (reg_write, mem_read, mem_write, branch, jump, alu_src, etc.)
- [x] ISA subset: arithmetic, logic, shifts, loads, stores, branches, jumps
- [x] Program loader for flat instruction arrays
- [x] Unit tests (38 checks on known programs)

### ✅ Stage 1.5 — IProcessor Refactor & Pipelined Backend

- [x] Abstract `IProcessor` interface (Ripes/DrMIPS pattern)
- [x] `PipelineState` observable (IF/ID/EX/MEM/WB snapshots, forwarding flags, hazard indicators)
- [x] SingleCycleCpu: old logic now implements IProcessor
- [x] PipelinedCpu: full 5-stage pipeline
- [x] Forwarding unit (EX/MEM → EX, MEM/WB → EX)
- [x] Hazard detection (load-use stall, branch/jump flush)
- [x] Polymorphic test harness (95 checks, both implementations)
- [x] Backward-compat shim (`using Cpu = SingleCycleCpu`)

### 🟡 Stage 2 — TUI Execution Visualizer & CPU Switcher

- [x] CPU mode switcher (select single-cycle or pipelined at runtime)
- [x] Register file panel (all 32 registers, last-written highlighted)
- [x] Datapath panel (PC, current instruction, cycle counter, CPU mode)
- [x] Pipeline visualization (WebRISC-V style: IF/ID/EX/MEM/WB state per cycle, forwarding paths, hazard flags)
- [ ] Memory panel (hex dump, PC-highlighted address)
- [ ] Adjustable run speed
- [ ] Instruction panel (disassembled fields: opcode, rs, rt, rd, shamt, funct)

### Stage 3 — Assembler

- [ ] Two-pass assembler with symbol table
- [ ] Label resolution for branches and jumps
- [ ] Pseudo-instructions (li, move, la, nop)
- [ ] Inline editor or file loader in TUI

### Stage 4 — Advanced Visualizations (Arches, WebRISC-V patterns)

- [ ] Instruction × cycle grid (WebRISC-V): show which instruction occupies each stage per cycle
- [ ] Per-module telemetry layer (stall type logging, CPI readout)
- [ ] Cycle-level stall counters (data hazard, control hazard, load-use)

### Stage 5 — Branch Prediction (PSBE / Pipelined MIPS Simulation pattern)

- [ ] 1-bit and 2-bit saturating predictors
- [ ] Branch Target Buffer (BTB)
- [ ] Misprediction flush visualization

### Stretch Goals

- [ ] Configurable datapath from external description file (DrMIPS pattern)
- [ ] RISC-V backend behind the same visualizer (Ripes pattern)
- [ ] Memory-mapped I/O (console output)
- [ ] Breakpoints and watchpoints
- [ ] Cycle counters and CPI analysis
- [ ] Reverse execution with snapshots (DrMIPS feature)

## 🧱 Design Conventions

- **Headers are `.h`, sources are `.cpp`** — one component per pair
- **Strong enums** (`enum class`) for hardware fields — no bare integers
- **`std::variant`** for instruction fields (invalid access throws, not silent garbage)
- **`std::optional`** for fallible operations (decode, control, ALU) — no sentinels
- **`[[nodiscard]]`** on pure queries
- **Core libraries never include UI headers** — `mips_core` and `nsc_core` are pure logic
- **`constexpr` where feasible** — bit-field extraction, encoding tables
- **Polymorphic abstraction** (`IProcessor`) to isolate backend choice from frontend

## 📄 License

see [LICENSE](LICENSE) file