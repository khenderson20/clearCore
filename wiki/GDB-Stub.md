# GDB Stub (Remote Serial Protocol)

clearCore includes a built-in **GDB Remote Serial Protocol (RSP) server** that lets you connect a real `mips-linux-gnu-gdb` or `gdb-multiarch` instance to the emulator. This means you can set breakpoints, single-step, inspect and modify registers and memory, and examine the call stack — all from an industry-standard debugger rather than the emulator's own UI.

This is modelled on the same mechanism that QEMU exposes with `-s -S`.

The stub is a C++ API in `mips_core`. None of the three front ends starts it yet, and the release downloads do not include the library, so you run it from a small host program that you build from a clearCore checkout.

## Quick start

`gdb_host.cpp` loads an ELF file into a CPU model and serves it to GDB:

```cpp
#include "mips/elf_loader.h"
#include "mips/gdb_stub.h"
#include "mips/single_cycle_cpu.h"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: gdb_host <program.elf>\n";
        return 2;
    }
    mips::SingleCycleCpu cpu(4u << 20);  // 4 MiB of memory
    std::string          err;
    if (!mips::load_elf_file_into_processor(cpu, argv[1], err)) {
        std::cerr << "ELF load failed: " << err << '\n';
        return 1;
    }
    mips::GdbStub stub(cpu);  // listens on 127.0.0.1:1234
    stub.listen();            // returns when GDB detaches or kills the session
}
```

Build it with a `CMakeLists.txt` in the same directory, which pulls in your clearCore checkout as a subdirectory (here, `../clearCore`):

```cmake
cmake_minimum_required(VERSION 3.20)
project(gdb_host LANGUAGES CXX)

# Only the core library is needed: skip the GUIs, the LLVM bridge and the MARS tests.
set(BUILD_QT6_UI       OFF CACHE BOOL "" FORCE)
set(BUILD_QT6_QUICK_UI OFF CACHE BOOL "" FORCE)
set(BUILD_NYXSTONE     OFF CACHE BOOL "" FORCE)
set(GOLDEN_TESTS       OFF CACHE BOOL "" FORCE)
add_subdirectory(../clearCore clearcore)

add_executable(gdb_host gdb_host.cpp)
target_link_libraries(gdb_host PRIVATE mips_core)
target_compile_features(gdb_host PRIVATE cxx_std_20)
```

```bash
cmake -S . -B build -G Ninja
cmake --build build --target gdb_host
./build/gdb_host my_program
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
| GDB stub (`this feature`) | Instruction-accurate | Medium (build a small host program, then one `target remote` command) | Full GDB: backtraces, watchpoints, scripting |
| QEMU user-mode emulation | Instruction-accurate | High (need full MIPS sysroot) | Full GDB |
| MARS simulator | Instruction-accurate | Medium (Java) | MARS-only debugger |

The GDB stub's key advantage over MARS is that it works with the same clearCore CPU models that produce the pipeline visualisation — breakpoints and single-stepping interact with the real forwarding unit, hazard detector, and CP0 state, not a separate interpreter.

## Limitations

- **Single-threaded**: the CPU runs on the same OS thread as the RSP loop, and no pipeline visualizer is attached while GDB drives it. `continue` runs the CPU in a loop in `handle_continue()` that checks the socket every `kInterruptPollSteps` (1024) instructions, so Ctrl-C in GDB stops it with one SIGINT stop reply.
- **No hardware breakpoints**: only software breakpoints (`Z0`/`z0`) are supported. `Z1`–`Z4` return empty responses (GDB falls back gracefully).
- **No `vCont`**: GDB may warn about this. It falls back to `c`/`s` automatically.
- **Exceptions stop at the faulting instruction**: the stub reports each exception as a signal and sets the PC back to EPC, so GDB sees the instruction that trapped and no handler at `0x8000_0180` is needed. `continue` then re-executes that instruction; to get past a `SYSCALL` or `BREAK`, move the PC on in GDB (`set $pc = $pc + 4`).
