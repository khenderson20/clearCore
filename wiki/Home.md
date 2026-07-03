# clearCore

**clearCore** is an educational MIPS CPU *simulator* built in C++20 — not a binary-compatible emulator. It models the internal microarchitectural behavior of two textbook datapaths cycle by cycle, making processor internals visible and interactive for students learning computer architecture.

The project ships two primary interfaces over identical core logic: a lightweight terminal UI (FTXUI) and a full Qt6 desktop GUI. Both let you step through instructions, watch the pipeline fill and drain, and inspect every register and memory word at each cycle. An experimental Qt Quick/QML interface (`clearCore-quick`) also builds by default alongside them — see [Qt6 GUI](Qt6-GUI) for its status.

---

## What it does

- **Number system conversion** — real-time binary / hexadecimal / decimal conversion backed by a single `uint64_t` value
- **MIPS instruction decoder** — enter any 32-bit word and see opcode, register fields, and reconstructed assembly text
- **Single-cycle CPU** — classic non-pipelined datapath; one instruction from fetch to writeback per clock
- **5-stage pipelined CPU** — concurrent IF/ID/EX/MEM/WB execution with load-use stall detection, EX/MEM and MEM/WB forwarding, and branch/jump flush handling
- **Live hazard visualization** — stall, forward, and flush badges inline in the pipeline strip
- **Telemetry** — running cycle count, CPI, stall/forward/flush tallies
- **In-app MIPS assembler** — the Qt6 Code Editor assembles labeled assembly source directly into loadable instruction words (no external toolchain)
- **Differential testing** — the `tests/golden/` suite cross-checks both CPU models against MARS, the classroom-standard MIPS simulator
- **CP0 exception model** — SYSCALL, BREAK, overflow, address errors, and reserved-instruction faults raise MIPS32r2 exceptions; Status, Cause, EPC, and BadVAddr are fully modelled; ERET/MFC0/MTC0 are supported
- **ELF loader** — load `mipsel` (little-endian) ELF32 executables compiled with `mipsel-linux-gnu-gcc` or `mipsel-linux-musl-gcc` directly into the emulated address space
- **GDB RSP stub** — attach `mipsel-linux-gnu-gdb` to port 1234 for breakpoints, single-step, register/memory inspection, and exception-driven stop signals

---

## Quick navigation

| Page                                            | Description                                              |
|--------------------------------------------------|-----------------------------------------------------------|
| [Getting Started](Getting-Started)               | Build instructions, dependencies, first run               |
| [Architecture](Architecture)                     | Module layout, `IProcessor` interface, design pillars     |
| [MIPS CPU Simulator](MIPS-CPU-Emulator)          | Pipeline stages, hazard detection, forwarding, ISA subset |
| [CP0 and Exceptions](CP0-Exceptions)             | Exception model, SYSCALL/BREAK, MFC0/MTC0/ERET, EPC      |
| [ELF Loader](ELF-Loader)                         | Loading compiled MIPS binaries; mipsel toolchain guide    |
| [GDB Stub](GDB-Stub)                             | GDB RSP server — breakpoints, single-step, signals        |
| [Terminal UI](Terminal-UI)                       | FTXUI interface — tabs, controls, visualizations          |
| [Qt6 GUI](Qt6-GUI)                               | Desktop interface — views, signal flow, thread safety     |
| [Roadmap](Roadmap)                               | Stage-by-stage feature status and upcoming work            |
| [Contributing](Contributing)                     | Code style, testing conventions, PR workflow                |

---

## At a glance

```
C++20 · CMake 3.20+ · MIT license
FTXUI v7.0.0 (auto-fetched) · Qt6 (Widgets + Quick, both optional) · Nyxstone/LLVM (optional)
Five core CTest suites + a Qt smoke-test suite + MARS differential tests
```

Build presets: `debug`, `release`, `asan`, `core-only` (TUI-only, no Qt/LLVM). See [Getting Started](Getting-Started).

**Topics:** `simulator` `cpu` `mips` `cpp` `cpp20` `educational` `computer-architecture` `terminal-ui` `qt6`

---

> **Note on "emulator" vs. "simulator":** clearCore models the Harris & Harris / Patterson & Hennessy textbook datapaths — it does not aim for binary compatibility with real MIPS silicon. The project intentionally calls itself a *simulator* throughout its documentation; if you see "emulator" anywhere in older material, that's the term this wiki update is retiring.
