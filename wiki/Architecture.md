# Architecture

clearCore is organized into independent libraries and interface layers that share no circular dependencies. The core simulation code has zero knowledge of any UI framework.

---

## Module overview

```
┌──────────────────────┬───────────────────────┬──────────────────────────┐
│ nsc_ui (FTXUI TUI)    │ nsc_qt (Qt6 Widgets)  │ nsc_quick (Qt Quick/QML) │
└──────────┬────────────┴───────────┬───────────┴────────────┬─────────────┘
           │                        │                        │
           ▼                        ▼                        ▼
┌───────────────────────────────────────────────────────────────────────┐
│                               mips_core                               │
│  ┌─────────────────────────── isa:: (ISA-agnostic core) ────────────┐ │
│  │  IProcessor · Memory · RegisterFile · PipelineState · StepResult │ │
│  └──────────────────────────────────────────────────────────────────┘ │
│   mips::IMipsProcessor ◄── SingleCycleCpu / PipelinedCpu (adds CP0/HI/LO)│
│   Decoder · ALU · Control · CP0 · Disassembler · trace (spdlog)        │
│   (optional) NyxstoneBackend (LLVM-based assembler/disassembler)      │
└──────────────────────────────┬────────────────────────────────────────┘
                                │
                    ┌───────────┘
                    ▼
        ┌─────────────────────────┐
        │        nsc_core         │
        │  Number system converter│
        └─────────────────────────┘
```

| Library / target | Responsibility                                                                     |
|-------------------|-------------------------------------------------------------------------------------|
| `nsc_core`        | Real-time binary/hex/decimal conversion around a `uint64_t` value                   |
| `mips_core`       | CPU simulation: the ISA-agnostic `isa::` core (`IProcessor`, `Memory`, `RegisterFile`, `PipelineState`) plus the MIPS backend — decoder, ALU, disassembler, both CPU models |
| `nsc_ui`          | FTXUI terminal interface (`number_system_converter` binary) — depends only on public `mips_core` headers |
| `nsc_qt`          | Qt6 Widgets layer (`clearCore-gui` binary) — `SimulatorController` bridges CPU state to Qt signals, plus an in-app MIPS assembler |
| `nsc_quick`       | Qt Quick / QML layer (`clearCore-quick` binary) — same `IProcessor` backend, declarative UI in `qml/ClearCore/` |

**Rule:** `nsc_core` and `mips_core` must never include UI headers. This boundary is enforced in `CMakeLists.txt` through target link dependencies.

All three UI targets, plus `BUILD_NYXSTONE` (LLVM-based assembler/disassembler, LLVM 15–20) and `GOLDEN_TESTS` (MARS differential testing), default to **ON** and degrade gracefully — the build still configures and the TUI still builds even if Qt6, an in-range LLVM, or a JRE is missing. See [Getting Started](Getting-Started) for the CMake options.

---

## Three design pillars

### 1. Separation of concerns

Each library is independently unit-testable. You can run the decoder, ALU, and both CPU backends without any terminal or window on screen. Adding a new UI requires implementing nothing in the core — clearCore already demonstrates this with three separate UI layers (`nsc_ui`, `nsc_qt`, `nsc_quick`) over one simulation core.

### 2. Pluggable-backend pattern

The processor contract is split in two so a second ISA can reuse the whole stack. `isa::IProcessor` is the **ISA-agnostic** contract every backend implements; `mips::IMipsProcessor` layers the MIPS-only architectural state (coprocessor 0, HI/LO, the MIPS `Control` word) on top.

```cpp
namespace isa {
// ISA-agnostic — any backend (MIPS today, RISC-V next) implements this.
class IProcessor {
public:
    virtual StepResult step()                                            = 0;
    virtual bool       load_program(const std::vector<uint32_t>&,
                                    uint32_t addr = 0)                    = 0;
    virtual void       reset(bool clear_memory = false)                  = 0;
    virtual const PipelineState& pipeline_state() const noexcept         = 0;
    // pc(), regs(), mem(), cycle_count() ...
};
}  // namespace isa

namespace mips {
// MIPS adds coprocessor 0, HI/LO, and the last committed Control word.
class IMipsProcessor : public isa::IProcessor {
public:
    virtual const Control& last_control() const noexcept = 0;
    virtual Cp0&           cp0() noexcept                 = 0;
    virtual uint32_t       hi() const noexcept            = 0;  // ... lo(), set_hi/lo()
};

class SingleCycleCpu : public IMipsProcessor { /* ... */ };
class PipelinedCpu   : public IMipsProcessor { /* ... */ };
}  // namespace mips
```

Every UI holds an `isa::IProcessor*` (re-exported as `mips::IProcessor` for existing callers). Switching between single-cycle and pipelined mode at runtime is a pointer swap — no UI changes. A future RISC-V core derives straight from `isa::IProcessor` (plus its own CSR sub-interface) and reuses `isa::Memory`, `isa::RegisterFile`, and `isa::PipelineState`, so all three visualizers work unchanged. This pattern is inspired by [Ripes](https://github.com/mortbopet/Ripes) and [DrMIPS](https://brunonova.github.io/drmips/).

### 3. Observable pipeline state

`PipelineState` is a plain struct that snapshots all five pipeline stages, forwarding-path selections, and hazard flags in one place. Every visualizer — the TUI pipeline strip, the Qt6 Datapath tab, the Pipeline Trace grid — reads from `PipelineState`. There is no separate "visualization model"; the simulation state *is* the model.

---

## Control signal derivation

A single `derive_control()` free function maps an opcode/funct pair to the full set of hardware control signals (RegWrite, MemRead, MemWrite, Branch, Jump, ALUSrc, ALUOp, etc.). Both `SingleCycleCpu` and `PipelinedCpu` call it, which prevents the two backends from drifting apart on control signal semantics.

---

## ISA-agnostic core (`isa::`)

These types carry nothing MIPS-specific and live in `include/isa/`, ready to be shared by a second ISA backend (RISC-V). The `mips` headers re-export them with `using`-shims (`mips::Memory`, `mips::RegisterFile`, `mips::IProcessor`, …) so existing callers compile unchanged.

| Component        | File(s)                   | Role                                                        |
|-------------------|----------------------------|---------------------------------------------------------------|
| `isa::IProcessor`  | `include/isa/processor.h` | ISA-agnostic processor contract + `PipelineState` / `StepResult` / `StageSnapshot` |
| `isa::RegisterFile`| `include/isa/registers.h` | 32 × `uint32_t`, register 0 hardwired                         |
| `isa::Memory`      | `include/isa/memory.h`    | Byte-addressable, word/half/byte access, alignment checks     |

## `mips_core` internals

| Component            | File(s)                     | Role                                                        |
|-----------------------|------------------------------|---------------------------------------------------------------|
| `mips::IMipsProcessor`| `include/mips/processor.h`  | Extends `isa::IProcessor` with CP0, HI/LO, and the MIPS `Control` word |
| `Decoder`             | `include/mips/decoder.h`    | 32-bit word → instruction fields + mnemonic                   |
| `ALU`                 | `include/mips/alu.h`        | Arithmetic, logic, shifts, overflow flags                     |
| `register_abi_name`   | `include/mips/registers.h`  | MIPS O32 ABI mnemonic table (the ISA-specific part left in `mips`) |
| `Disassembler`    | `include/mips/disassembler.h`    | Machine code → assembly text; hex program loader              |
| `SingleCycleCpu`  | `src/mips/single_cycle_cpu.cpp`  | Simulates all five stages in one `step()` call                |
| `PipelinedCpu`    | `src/mips/pipelined_cpu.cpp`     | Concurrent pipeline with five pipeline registers               |
| Hazard detection  | (internal to `PipelinedCpu`)     | Load-use stall detection, branch/jump flush                    |
| Forwarding unit   | (internal to `PipelinedCpu`)     | EX/MEM → EX and MEM/WB → EX forwarding paths                   |
| Trace log         | `include/mips/trace.h`           | Shared spdlog logger for instruction/pipeline/exception tracing; quiet by default, enable with `CLEARCORE_LOG_LEVEL` |
| `NyxstoneBackend` | `include/mips/nyxstone_backend.h` (optional, `CLEARCORE_NYXSTONE_ENABLED`) | LLVM-based text ↔ machine-code assembler/disassembler; pimpl'd so no LLVM headers leak. Differentially validates the Decoder/Disassembler (see `nyxstone_test`). Built when LLVM 15–20 is found at configure time |

---

## Qt6 bridge (`nsc_qt`)

The Qt6 Widgets layer adds a `SimulatorController` object that:

1. Runs the CPU in a background thread
2. Emits Qt signals (e.g. pipeline state, register, and memory changes) as state changes
3. Qt's event loop automatically marshals those signals to the main (UI) thread

Widget code never touches the CPU directly and never needs a mutex. `nsc_qt` also owns the in-app MIPS assembler (`assembler.h`/`.cpp`) used by the Code Editor tab. See [Qt6 GUI](Qt6-GUI) for the full breakdown, including the parallel Qt Quick (`nsc_quick`) interface.

---

## Testing architecture

Beyond the seven core CTest suites (`decoder_test`, `cpu_test`, `processor_test`, `disasm_test`, `cp0_test`, `elf_loader_test`, `nsc_tests`, plus `gdb_stub_test` and `nyxstone_test` built only when their features are enabled) and the Qt smoke-test suite (see [Getting Started](Getting-Started)), `tests/golden/` runs **differential tests**: each `.asm` program in that directory is assembled and executed independently by MARS (a Java-based reference MIPS simulator) and by both clearCore CPU models via `golden_runner`, and the resulting register files are compared for an exact match. This is separate from — and a stronger correctness signal than — the polymorphic `IProcessor` contract tests, which only check the two clearCore backends against each other rather than against an external reference. Golden tests are skipped automatically if no JRE or Python 3 is available.

**Fuzz testing**: `tests/fuzz/fuzz_hex_loader.cpp` is a libFuzzer harness targeting `mips::parse_hex_program`. It is not part of the normal CTest runs; it is built and exercised by the ClusterFuzzLite CI workflow (`.github/workflows/cflite_pr.yml`) on every PR. See [Contributing § Fuzzing](Contributing#fuzzing) for how to add new harnesses.

---

## Academic foundations

| Pattern                        | Source                                                            |
|---------------------------------|---------------------------------------------------------------------|
| Pluggable-backend `IProcessor` | Ripes (Petersen, 2021)                                              |
| Observable `PipelineState`     | WebRISC-V (Mariotti & Giorgi, 2022); Arches (Haydel et al., 2025)   |
| Single-cycle datapath design   | Harris & Harris, *Digital Design and Computer Architecture*         |
| Pipeline hazard handling       | Patterson & Hennessy, *Computer Organization and Design*            |
| Differential/golden testing    | MARS (Vollmar & Sanderson) as reference oracle                       |
