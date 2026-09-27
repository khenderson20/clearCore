# CP0 and the MIPS Exception Model

clearCore's `mips_core` library now implements a minimal **Coprocessor 0 (CP0)** that models the MIPS32r2 exception architecture. Every `IProcessor` implementation — both `SingleCycleCpu` and `PipelinedCpu` — integrates with CP0 so exceptions are raised, recorded, and observable in the same way real MIPS hardware handles them.

## Why exceptions matter for an emulator

Before CP0, any bad instruction or memory access returned `StepResult::Fault` and stopped the CPU cold. That makes debugging impossible: there is no context to inspect, no way to recover, and no signal to a debugger. The MIPS hardware exception model solves all three:

- **Recoverable:** the faulting PC is saved in EPC. An exception handler (or GDB) can inspect the problem and resume.
- **Observable:** the Cause register names the exception type; BadVAddr records the faulting address for address errors.
- **Composable with the GDB stub:** the stub intercepts exceptions and translates them to GDB signals (SIGTRAP for BREAK, SIGSYS for SYSCALL, SIGSEGV for address errors), enabling real debugger-driven inspection.

## Supported exception types

| Code | Name  | Trigger |
|------|-------|---------|
| 0    | Int   | Hardware interrupt (not generated internally; modelled for completeness) |
| 4    | AdEL  | Address error on instruction fetch or data load (OOB, misaligned) |
| 5    | AdES  | Address error on data store (OOB, misaligned) |
| 8    | Sys   | `SYSCALL` instruction |
| 9    | Bp    | `BREAK` instruction |
| 10   | RI    | Reserved (unrecognised) instruction |
| 12   | Ov    | Signed arithmetic overflow (`ADD`, `ADDI`, `SUB`) |

## New instructions

| Instruction | Encoding | Behaviour |
|-------------|----------|-----------|
| `SYSCALL`   | `SPECIAL funct=0x0C` | Raises `ExceptionCode::Sys`. Conventionally: $v0 holds the syscall number, $a0–$a3 hold arguments. |
| `BREAK`     | `SPECIAL funct=0x0D` | Raises `ExceptionCode::Bp`. Used by GDB for software breakpoints. |
| `MFC0 rt, rd` | `COP0 rs=0` | Moves CP0 register `rd` into GPR `rt`. |
| `MTC0 rt, rd` | `COP0 rs=4` | Moves GPR `rt` into CP0 register `rd`. |
| `ERET`      | `COP0 rs=0x10 funct=0x18` | Clears `Status.EXL`, restores PC from EPC. Used at the end of every exception handler. |

## CP0 registers

| Reg # | Name     | Accessible via | Meaning |
|-------|----------|----------------|---------|
| 8     | BadVAddr | `MFC0 rt, $8`  | Address that caused the most recent AdEL or AdES exception |
| 12    | Status   | `MFC0`/`MTC0 rt, $12` | `EXL` bit (bit 1) is 1 while inside an exception handler |
| 13    | Cause    | `MFC0 rt, $13` | ExcCode field [6:2] holds the exception code |
| 14    | EPC      | `MFC0`/`MTC0 rt, $14` | Return address for ERET |

## Exception vector

Real MIPS hardware vectors to `0x8000_0180` (Status.BEV=0). clearCore uses this same address. Because the default memory size is 64 KB, you must either:

1. **Run under the GDB stub** — the stub intercepts exceptions before the CPU fetches from the vector, so no handler code is needed at `0x8000_0180`.
2. **Increase memory size** — pass a larger `mem_bytes` value to the `IProcessor` constructor, then map handler code at the vector address.
3. **ERET in the handler** — for testing, load a minimal handler (`ERET` at `0x8000_0180`) to return execution to EPC+4.

## StepResult changes

`StepResult::Exception` is the new return value for any step where a MIPS exception was raised. The previous `Fault` return (for truly unrecoverable internal errors) is still present but no longer returned for address errors or unrecognised instructions — those are now proper CP0 exceptions.

```cpp
switch (cpu.step()) {
case StepResult::Ok:        /* normal */         break;
case StepResult::Halt:      /* self-jump */      break;
case StepResult::Exception: /* CP0 exception */
    std::cout << "Exception: " << mips::exception_name(cpu.cp0().last_exception())
              << " EPC=0x" << std::hex << cpu.cp0().epc() << "\n";
    break;
case StepResult::Fault:     /* internal error */ break;
}
```

## Example: writing a minimal syscall handler

```mips
# Entry point
main:
    ori  $v0, $zero, 1         # syscall 1 = "print int"
    ori  $a0, $zero, 42
    syscall                    # raises Sys exception → vectors to 0x8000_0180

# Exception handler (must be mapped at 0x8000_0180)
exc_handler:
    mfc0 $k0, $13              # read Cause
    andi $k0, $k0, 0x7C        # extract ExcCode field
    srl  $k0, $k0, 2
    ori  $k1, $zero, 8         # 8 = Sys (SYSCALL)
    bne  $k0, $k1, not_syscall
    # handle syscall: read $v0, dispatch ...
    addiu $k0, $14, 4          # EPC + 4 = instruction after SYSCALL
    mtc0 $k0, $14
not_syscall:
    eret                       # clear EXL, restore PC from EPC
```

## API reference

```cpp
#include "mips/cp0.h"
#include "mips/processor.h"

// After step() returns Exception:
const mips::Cp0& cp0 = cpu.cp0();
cp0.last_exception();   // ExceptionCode enum value
cp0.epc();             // faulting instruction address
cp0.cause();           // raw Cause register
cp0.bad_vaddr();       // for AdEL/AdES: the bad address
cp0.in_exception();    // true while Status.EXL == 1

// MFC0/MTC0 via the register number interface:
cp0.read(mips::Cp0::kRegStatus);
cp0.write(mips::Cp0::kRegEpc, new_epc);
```
