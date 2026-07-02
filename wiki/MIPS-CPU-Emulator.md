# MIPS CPU Simulator

> This page is still linked from the sidebar as "MIPS CPU Emulator" — the slug is unchanged to avoid breaking existing links, but the project consistently uses **simulator**, not emulator: clearCore models the H&H/P&H textbook datapaths cycle-by-cycle rather than aiming for binary compatibility with real MIPS silicon.

clearCore simulates a subset of the 32-bit MIPS ISA. Two CPU implementations coexist behind the `IProcessor` interface: a simple single-cycle datapath and a full 5-stage pipeline.

---

## Supported ISA subset

This table reflects exactly what `include/mips/decoder.h` decodes — the opcode and funct enums are the ground truth.

| Category        | Instructions                                       |
|-------------------|-------------------------------------------------------|
| Arithmetic        | `ADD`, `ADDU`, `SUB`, `SUBU`, `ADDI`, `ADDIU`         |
| Logic             | `AND`, `OR`, `NOR`, `XOR`, `ANDI`, `ORI`, `XORI`      |
| Shifts            | `SLL`, `SRL`, `SRA`, `SLLV`, `SRLV`                    |
| Comparison        | `SLT`, `SLTU`, `SLTI`, `SLTIU`                          |
| Memory            | `LW`, `SW`, `LBU`, `LHU`                                |
| Branches          | `BEQ`, `BNE`                                            |
| Jumps             | `J`, `JAL`, `JR`, `JALR`                                |
| Upper immediate   | `LUI`                                                    |

**Not currently decoded**, despite appearing in the full MIPS I ISA (and in some earlier project notes): `SRAV`; the signed/unsigned byte and halfword pairs `LB`/`SB`/`LH`/`SH` (only the unsigned loads `LBU`/`LHU` and the word ops `LW`/`SW` exist); and the remaining conditional branches `BGTZ`, `BLEZ`, `BGEZ`, `BLTZ`. Adding any of these is a `Decoder::decode()` + `derive_control()` change, exercised through the `processor_test.cpp` polymorphic harness — see [Contributing](Contributing).

The decoder handles all three MIPS instruction formats:

- **R-format** — `opcode(6) | rs(5) | rt(5) | rd(5) | shamt(5) | funct(6)`
- **I-format** — `opcode(6) | rs(5) | rt(5) | imm(16)`
- **J-format** — `opcode(6) | target(26)`

---

## Single-cycle CPU

`SingleCycleCpu` simulates the full datapath in a single `step()` call:

```
Fetch → Decode → Control → Execute → Memory → Writeback
```

Every instruction takes exactly one cycle. This makes the control flow easy to follow and is the recommended starting point when learning the simulator.

The control unit generates all signals (`RegWrite`, `MemRead`, `MemWrite`, `Branch`, `Jump`, `ALUSrc`, `ALUOp`, `MemToReg`, `RegDst`) from the decoded opcode/funct pair via `derive_control()`. The ALU result and memory data are computed in the same call and written back to the register file before `step()` returns.

---

## Pipelined CPU

`PipelinedCpu` runs five instructions concurrently, one per stage:

```
Cycle N:    IF      ID      EX      MEM     WB
Cycle N+1:  IF      ID      EX      MEM     WB
              ↑       ↑       ↑       ↑       ↑
          instr5  instr4  instr3  instr2  instr1
```

Each stage reads from a pipeline register (a struct holding the outputs of the previous stage) and writes into the next one. Between cycles, the pipeline registers shift forward.

### Pipeline registers

| Register | Contents                                                              |
|----------|--------------------------------------------------------------------------|
| `IF/ID`  | Fetched instruction word, incremented PC                                  |
| `ID/EX`  | Decoded register values, sign-extended immediate, control signals        |
| `EX/MEM` | ALU result, zero flag, forwarded operands, control signals               |
| `MEM/WB` | Memory read data or ALU result, destination register, control signals    |

### Hazard detection

#### Load-use stall

When an `LW` instruction is in `EX` and the immediately following instruction reads the register being loaded, the pipeline cannot forward in time (the memory value won't exist until the end of the `MEM` stage). The hazard unit:

1. Freezes the `IF/ID` register (re-fetches the same instruction next cycle)
2. Inserts a **bubble** (NOP) into the `EX` stage
3. Holds the PC constant

This inserts one stall cycle. After the stall, forwarding from `MEM/WB` covers the dependency.

#### Branch and jump flush

Branches are resolved in the `EX` stage. When a branch is taken (or an unconditional jump is decoded), instructions already in `IF` and `ID` are invalid. The hazard unit **flushes** those two stages by zeroing the pipeline registers, discarding two in-flight instructions.

### Data forwarding

Forwarding eliminates most stalls by routing a result directly to the ALU input of a dependent instruction before it has been written back to the register file.

| Path            | When it fires                                                                             |
|-------------------|-----------------------------------------------------------------------------------------------|
| **EX/MEM → EX**  | The instruction in `EX` reads a register that the instruction in `EX/MEM` just computed        |
| **MEM/WB → EX**  | The instruction in `EX` reads a register that the instruction in `MEM/WB` produced              |

The forwarding unit checks both paths every cycle and selects the correct operand source for each ALU input (register file, EX/MEM forward, or MEM/WB forward).

---

## PipelineState snapshot

After each `step()` call, `PipelinedCpu::pipeline_state()` returns a `PipelineState` struct containing a `StageSnapshot` for each of the five stages, plus stall / forwarding / flush flags for that cycle. Both the TUI and both Qt UIs read this struct to drive their visualizations — no separate state extraction is needed.

---

## Execution trace

The CPU maintains an 8-entry ring buffer of the last committed instructions (sourced at the WB stage). The TUI renders this as a scrolling execution history panel; the Qt6 GUI shows it in the Pipeline Trace tab.

---

## Telemetry

Each `step()` increments internal counters: total cycles elapsed, stall bubbles inserted, forwarding operations performed, and pipeline stages flushed. CPI is derived as cycles / instructions committed. Counters reset on `reset()` or when the CPU mode switches. Both the TUI dashboard and the Qt6 Statistics tab read these counters.

---

## Loading programs

Programs are flat arrays of 32-bit instruction words. Load them via:

- **TUI Program Loader tab** — paste hex words one per line
- **Qt6 Code Editor** — write MIPS assembly with labels and branches; the in-app assembler (`nsc_qt::assemble()`) emits the word array. It supports the full instruction set above plus label resolution, but not pseudo-instructions or assembler directives (`.data`/`.text`/`.word`) yet — that's Stage 3, see [Roadmap](Roadmap).
- **Nyxstone** (when `BUILD_NYXSTONE=ON` and LLVM 15+ is available) — an LLVM-based assembler/disassembler bridge used internally for test generation and text↔machine-code conversion
- **`IProcessor::load(std::vector<uint32_t>)`** — call directly from C++ for testing

Memory is byte-addressable. The program is placed starting at address `0x00000000`. The stack conventionally grows down from `0xFFFFFFFF`.

---

## Halt detection

The CPU detects halt using the idiom:

```
loop: j loop   # jump to self
```

When the PC jumps to its own address and stays there, the UI reports `StepResult::Halt` and stops auto-run.
