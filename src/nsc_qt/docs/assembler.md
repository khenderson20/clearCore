# assembler.h / assembler.cpp

## A. Overview

This file implements a small, self-contained two-pass MIPS assembler used exclusively by the Qt6 GUI's Code Editor tab. It turns multi-line MIPS assembly text into the raw 32-bit instruction words `SimulatorController::loadProgram()` expects, so users can write or paste assembly directly into the GUI rather than hand-assembling or using an external toolchain. A developer reaches for `assemble()` any time MIPS assembly source text needs to become loadable instruction words.

The implementation is plain C++20 with no Qt dependency — it operates on `std::string`/`std::string_view`, not `QString` — which keeps it testable and reusable independent of the GUI (`tests/qt_ui/qt_ui_test.cpp` exercises it directly).

Pass 1 scans every line, strips comments, and records the word-index of every `label:` definition. Pass 2 encodes each instruction, resolving branch and jump targets (numeric or label) against the table built in pass 1 — so forward references (a branch to a label defined later in the source) work correctly, not just backward ones.

## B. Namespaces

- **`nsc::qt`** — the same namespace used by every other Qt-GUI-specific type in this project (widgets, `SimulatorController`, `MainWindow`). Groups this assembler with the rest of the GUI-only code, as distinct from the UI-agnostic `mips` namespace used by `mips_core`.

## C. Types and Type Aliases

| Name | Kind | Description |
|------|------|-------------|
| `AssemblerResult` | struct | The return type of `assemble()`. Holds the assembled `words` on success, or a populated `error` string ("line N: message") on failure. `ok()` and `explicit operator bool()` both return `!error.has_value()`, so a result can be checked directly in an `if`. |

## D. Constants

None declared in the header. (The register-name table and other lookup tables used internally are implementation details in the `.cpp`'s anonymous namespace, not part of the public API.)

## E. Functions

#### `AssemblerResult assemble(const std::string& source)`
Assembles `source` — a complete multi-line MIPS assembly program — into a sequence of 32-bit instruction words. Supports `add`, `addu`, `sub`, `subu`, `and`, `or`, `xor`, `nor`, `slt`, `sltu`, `sll`, `srl`, `sra`, `sllv`, `srlv`, `jr`, `jalr`, `addi`, `addiu`, `slti`, `sltiu`, `andi`, `ori`, `xori`, `lui`, `lw`, `lbu`, `lhu`, `sw`, `beq`, `bne`, `j`, `jal`, and `nop`. Labels (`name:`) may be used as branch/jump targets, including forward references. Registers may be written by ABI name (`$t0`, `$sp`, …) or numerically (`$8`, `$29`, …). Immediates accept decimal or `0x`-prefixed hexadecimal, with an optional leading `-`. Comments run from `#` to end of line; blank lines and comment-only lines are ignored. On the first malformed line, encoding stops and `AssemblerResult::error` is set to `"line N: <message>"`, where `N` is 1-based; `words` reflects only what was successfully encoded up to that point (undefined for further use — check `ok()` first). `[[nodiscard]]` — the caller must not discard the result without checking it.

## F. Dependencies

- `<cstdint>` — fixed-width integer types (`uint8_t`, `uint32_t`, `int32_t`) for register indices, immediates, and encoded words.
- `<optional>` — `AssemblerResult::error` and the internal register/immediate parsers' return types.
- `<string>` — `AssemblerResult::error`'s underlying type and the source parameter.
- `<vector>` — `AssemblerResult::words`.
- (`.cpp` only) `<algorithm>`, `<charconv>`, `<sstream>`, `<string_view>`, `<unordered_map>` — tokenizing, numeric parsing (`std::from_chars`), and the register-name/label lookup tables.

No Qt modules are required by this file.

## G. Usage Example

```cpp
#include "nsc_qt/assembler.h"

nsc::qt::AssemblerResult result = nsc::qt::assemble(
    "addi $t0, $zero, 5\n"
    "addi $t1, $zero, 10\n"
    "add  $t2, $t0, $t1\n");

if (result) {
    // result.words is ready to pass to SimulatorController::loadProgram()
} else {
    // result.error.value() is a "line N: message" diagnostic
}
```
