# GDB Stub (Remote Serial Protocol)

clearCore includes a built-in **GDB Remote Serial Protocol (RSP) server** that lets you connect a real `mips-linux-gnu-gdb` or `gdb-multiarch` instance to the emulator. This means you can set breakpoints, single-step, inspect and modify registers and memory, and examine the call stack — all from an industry-standard debugger rather than the emulator's own UI.

This is modelled on the same mechanism that QEMU exposes with `-s -S`.

## Quick start

```cpp
#include "mips/gdb_stub.h"
#include "mips/elf_loader.h"
#include "mips/single_cycle_cpu.h"

int main() {
    mips::SingleCycleCpu cpu(4u << 20);
    std::string err;
    mips::load_elf_file_into_processor(cpu, "my_program", err);

    // Start the GDB server on port 1234 (default) and wait for GDB to connect.
    mips::GdbStub stub(cpu);
    stub.listen();   // blocks until GDB sends 'k' (kill) or 'D' (detach)
}
```

Then in a second terminal:

```bash
# Connect GDB to the stub
mipsel-linux-gnu-gdb my_program
(gdb) target remote localhost:1234
(gdb) break _start
(gdb) continue
(gdb) info registers
(gdb) x/10i $pc
(gdb) stepi
```

## Supported RSP commands

| Command | Description |
|---------|-------------|
| `?`     | Stop reason (SIGTRAP) |
| `g`     | Read all 38 MIPS registers |
| `G`     | Write all registers |
| `p n`   | Read single register n |
| `P n=v` | Write single register n |
| `m addr,len` | Read `len` bytes from `addr` |
| `M addr,len:data` | Write bytes to memory |
| `c [addr]` | Continue execution (optionally from `addr`) |
| `s [addr]` | Step one instruction |
| `Z0,addr,kind` | Insert software breakpoint at `addr` |
| `z0,addr,kind` | Remove software breakpoint |
| `k` | Kill (exit the RSP loop) |
| `D` | Detach (exit the RSP loop, leave CPU running) |
| `qSupported` | Feature negotiation (`swbreak+`) |
| `qAttached` | Always `1` (attached to existing process) |
| `H`, `T` | Thread ops (ignored — single-threaded emulator) |

## MIPS register layout

GDB addresses 38 registers by number in the `g`/`G`/`p`/`P` commands:

| GDB register | MIPS name | Source |
|---|---|---|
| 0–31 | $zero, $at, $v0–$v1, $a0–$a3, $t0–$t9, $s0–$s7, $k0, $k1, $gp, $sp, $fp, $ra | `isa::IProcessor::regs()` |
| 32 | CP0 Status | `IMipsProcessor::cp0().status()` |
| 33 | LO | `IMipsProcessor::lo()` |
| 34 | HI | `IMipsProcessor::hi()` |
| 35 | CP0 BadVAddr | `IMipsProcessor::cp0().bad_vaddr()` |
| 36 | CP0 Cause | `IMipsProcessor::cp0().cause()` |
| 37 | PC | `isa::IProcessor::pc()` |

The stub holds a `mips::IMipsProcessor&` — the MIPS-specific interface — since it reads CP0, HI, and LO. The general-purpose registers, PC, and memory come from the ISA-agnostic `isa::IProcessor` base.

## Software breakpoints

GDB's `break` / `hbreak` commands use software breakpoints by default. The stub:

1. Reads the 4-byte word at the target address.
2. Saves it internally.
3. Writes the `BREAK` instruction (`0x0000000D`) in its place.

When the CPU executes `BREAK`, it raises a `Bp` exception. The stub catches `StepResult::Exception` with `ExceptionCode::Bp`, sends `T05` (SIGTRAP) to GDB with PC set to the faulting instruction's address, and waits for the next GDB command.

On `z0` (remove breakpoint), the original word is restored.

## Exception-to-signal mapping

| CP0 exception | GDB signal | Typical cause |
|---|---|---|
| `Bp` (BREAK) | SIGTRAP (5) | Software breakpoint or manual `break` instruction |
| `Sys` (SYSCALL) | SIGSYS (12) | Unhandled system call |
| `RI` (reserved instruction) | SIGILL (4) | Unrecognised opcode |
| `Ov` (overflow) | SIGFPE (8) | Signed arithmetic overflow |
| `AdEL` / `AdES` | SIGSEGV (11) | Bad memory address |

## Choosing a port

The default port is 1234 (same as QEMU):

```cpp
mips::GdbStub stub(cpu, 9000);  // listen on port 9000 instead
```

The stub binds to `127.0.0.1` only — it is not exposed on any network interface.

## Build configuration

The GDB stub is enabled by default but requires POSIX socket headers (`sys/socket.h`). It is automatically disabled if those headers are absent, with a CMake warning:

```cmake
cmake --preset debug -DBUILD_GDB_STUB=OFF   # disable explicitly
```

Code that conditionally uses the stub:

```cpp
#if CLEARCORE_GDB_STUB_ENABLED
    mips::GdbStub stub(cpu);
    stub.listen();
#endif
```

## Compared to other emulator debugging approaches

| Method | Breakpoint precision | Setup cost | Tooling |
|---|---|---|---|
| clearCore TUI step mode | Cycle-accurate | Zero | Built-in |
| GDB stub (`this feature`) | Instruction-accurate | Low (one `target remote` command) | Full GDB: backtraces, watchpoints, scripting |
| QEMU user-mode emulation | Instruction-accurate | High (need full MIPS sysroot) | Full GDB |
| MARS simulator | Instruction-accurate | Medium (Java) | MARS-only debugger |

The GDB stub's key advantage over MARS is that it works with the same clearCore CPU models that produce the pipeline visualisation — breakpoints and single-stepping interact with the real forwarding unit, hazard detector, and CP0 state, not a separate interpreter.

## Limitations

- **Single-threaded**: the CPU runs on the same OS thread as the RSP loop. `continue` runs the CPU in a tight loop in `handle_continue()`; the UI is paused while the CPU is running. For interactive use, pair with `step` (`si`/`ni`) or set a breakpoint near the target instruction.
- **No hardware breakpoints**: only software breakpoints (`Z0`/`z0`) are supported. `Z1`–`Z4` return empty responses (GDB falls back gracefully).
- **No `vCont`**: GDB may warn about this. It falls back to `c`/`s` automatically.
- **Exception vector must be mapped**: if the CPU takes an exception and there is no handler at `0x8000_0180`, the GDB stub catches the exception (sets PC back to EPC) before the next fetch, so GDB sees the faulting instruction rather than an OOB crash.
