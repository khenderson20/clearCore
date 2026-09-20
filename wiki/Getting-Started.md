# Getting Started

## Dependencies

| Dependency           | Version | Notes                                                                 |
|----------------------|---------|------------------------------------------------------------------------|
| C++ compiler         | C++20   | GCC 13+ or Clang 16+                                                    |
| CMake                | 3.20+   | 3.25+ recommended — the preset workflow below is the recommended path  |
| FTXUI                | v7.0.0  | Auto-fetched via `FetchContent` — no manual install needed             |
| Qt6                  | 6.x     | Optional — powers both `clearCore-gui` (Widgets) and `clearCore-quick` (QML); disable with `-DBUILD_QT6_UI=OFF -DBUILD_QT6_QUICK_UI=OFF` |
| KSyntaxHighlighting  | KF6     | Optional — adds MIPS syntax highlighting to the Qt6 Code Editor; auto-detected at configure time (`kf6-syntax-highlighting-devel` / `libkf6syntaxhighlighting-dev`) |
| LLVM + Clang         | 15–20   | Optional — powers the Nyxstone assembler/disassembler bridge; disable with `-DBUILD_NYXSTONE=OFF`. Nyxstone v0.1.8 supports **LLVM 15–20 only**; a newer system LLVM (21+) is auto-detected and Nyxstone is cleanly disabled unless you point it at an in-range install (see below). |
| Java + Python 3      | any     | Optional — needed only for the MARS differential ("golden") test suite; skipped automatically if missing |

GSL and spdlog are also auto-fetched via `FetchContent` and need no manual install. spdlog powers the runtime instruction/pipeline trace log — see [Runtime logging](#runtime-logging).

### Installing Qt6, LLVM, and KSyntaxHighlighting

Only needed if you want the desktop GUIs, the Nyxstone assembler bridge, or MIPS syntax highlighting — skip this if you only want the terminal UI.

```bash
# Fedora / RHEL
sudo dnf install qt6-qtbase-devel qt6-qtdeclarative-devel llvm19-devel clang19-devel kf6-syntax-highlighting-devel

# Ubuntu / Debian (KSyntaxHighlighting requires Ubuntu 25.10+)
sudo apt install qt6-base-dev qt6-declarative-dev llvm-19-dev libclang-19-dev libkf6syntaxhighlighting-dev

# macOS
brew install qt@6 llvm@19
```

KSyntaxHighlighting is auto-detected — omitting it is fine, the Code Editor just stays plain text.

#### Pinning an in-range LLVM for Nyxstone

If your distribution's **default** LLVM is newer than 20 (e.g. Fedora ships LLVM 22 as the default `llvm-devel`), install an in-range compat package and point Nyxstone at it with the `NYXSTONE_LLVM_PREFIX` environment variable — Nyxstone then searches that prefix exclusively:

```bash
# Fedora example: default LLVM is 22, but llvm19 is installed side-by-side
NYXSTONE_LLVM_PREFIX=/usr/lib64/llvm19 cmake --preset debug
```

Without this, configuration prints a clear warning and continues with Nyxstone disabled — the TUI, GUIs, and all other tests build normally.

---

## Building

### Recommended: CMake presets

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

| Preset      | What it builds                                                  |
|-------------|--------------------------------------------------------------------|
| `debug`     | Debug symbols, TUI + both GUIs (everything default-ON)             |
| `release`   | Optimized release build                                            |
| `asan`      | Debug build + AddressSanitizer/UBSanitizer (needs `libasan`/`libubsan`) |
| `core-only` | `mips_core`/`nsc_core` + TUI only — skips Qt6, Qt Quick, and LLVM entirely |

### Manual configure

```bash
cmake -S . -B cmake-build-debug
cmake --build cmake-build-debug
```

### Build a single target

```bash
# Terminal UI only
cmake --build cmake-build-debug --target number_system_converter

# Qt6 Widgets GUI only
cmake --build cmake-build-debug --target clearCore-gui

# Qt Quick / QML GUI only
cmake --build cmake-build-debug --target clearCore-quick
```

### Build options

All default **ON** except sanitizers:

| Option                    | Effect                                          |
|----------------------------|---------------------------------------------------|
| `-DBUILD_QT6_UI=OFF`       | Skip the Qt6 Widgets GUI (`clearCore-gui`)         |
| `-DBUILD_QT6_QUICK_UI=OFF` | Skip the Qt Quick / QML GUI (`clearCore-quick`)    |
| `-DBUILD_NYXSTONE=OFF`     | Skip the LLVM-based Nyxstone assembler/disassembler bridge |
| `-DGOLDEN_TESTS=OFF`       | Skip the MARS differential test suite               |
| `-DENABLE_SANITIZERS=ON`   | Build with AddressSanitizer + UBSanitizer (default `OFF`; the `asan` preset sets this for you) |

If Qt6 or LLVM aren't found during configuration, the corresponding target is silently skipped and the rest of the build (including the TUI) still succeeds.

---

## Running

```bash
# Terminal UI
./cmake-build-debug/number_system_converter

# Qt6 Widgets desktop GUI
./cmake-build-debug/clearCore-gui

# Qt Quick / QML desktop GUI
./cmake-build-debug/clearCore-quick
```

> **Terminal note:** FTXUI requires an ANSI-capable terminal. If running from an IDE, enable *Emulate terminal in output console* or launch from the shell.

---

## Runtime logging

`mips_core` emits a structured trace log (via **spdlog**) covering instruction fetch/decode (with disassembly), pipeline stall/flush/hazard events, CP0 exception raises, and memory faults. It is **quiet by default** (level `warn`) so normal runs, the UIs, and tests are unaffected.

Opt in at runtime with the `CLEARCORE_LOG_LEVEL` environment variable — accepted values are spdlog's level names (`trace`, `debug`, `info`, `warn`, `error`, `critical`, `off`). Output goes to **stderr**, keeping stdout clean for program output:

```bash
# Full per-instruction + pipeline-event trace
CLEARCORE_LOG_LEVEL=trace ./cmake-build-debug/number_system_converter

# Just exceptions and memory faults
CLEARCORE_LOG_LEVEL=debug ./cmake-build-debug/number_system_converter
```

Example trace output (pipelined model):

```
[clearcore T] pl  cyc=3      IF pc=0x00000008  0x01094820  add $t1, $t0, $t1
[clearcore T] pl  cyc=4      load-use hazard: stall, bubble into ID/EX
[clearcore D] exception AdEL raised: epc=0x00002000 bad_vaddr=0xdeadbeef -> vector 0x80000180
```

---

## Running tests

```bash
ctest --preset debug              # all suites
ctest --preset asan               # same suites under ASan/UBSan
```

Or manually:

```bash
cmake --build cmake-build-debug --target decoder_test cpu_test processor_test disasm_test cp0_test elf_loader_test nsc_tests qt_ui_test
ctest --test-dir cmake-build-debug --output-on-failure
```

The suite covers:

- `decoder_test` — R/I/J format decoding, opcode + funct → mnemonic
- `cpu_test` — CPU execution against known programs (both models)
- `processor_test` — polymorphic harness running both `SingleCycleCpu` and `PipelinedCpu` through identical programs via the `IProcessor` contract
- `disasm_test` — disassembler and hex program loader
- `cp0_test` — Coprocessor 0 exception model (SYSCALL/BREAK/overflow/address errors, MFC0/MTC0/ERET)
- `elf_loader_test` — MIPS ELF32 parsing and segment mapping
- `nyxstone_test` — differential validation of the Decoder + Disassembler against Nyxstone (LLVM's assembler): our disassembly of each corpus word is re-encoded by LLVM and asserted bit-identical. Built only when `BUILD_NYXSTONE=ON` and an in-range LLVM was found; self-skips otherwise.
- `nsc_tests` — number system converter (`parseBase`, conversions)
- `qt_ui_test` — Qt6 assembler/controller/widget smoke tests (built only when `BUILD_QT6_UI=ON`; runs headless via `QT_QPA_PLATFORM=offscreen`)
- `gdb_stub_test` — GDB RSP stub protocol handling (built only when `BUILD_GDB_STUB=ON`)

**MARS golden tests** (`golden_arith_single`, `golden_fib_pipelined`, etc.) run each program in `tests/golden/` through MARS — the classroom-standard reference MIPS simulator — and both clearCore CPU models, and assert the register files match exactly. These require a JRE and Python 3; CMake downloads MARS automatically and verifies its checksum, and quietly skips the suite if the runtime isn't available or the download fails.

This project uses a lightweight, dependency-free `CHECK()`-macro test harness throughout — not GoogleTest or Catch2.

### ClusterFuzzLite fuzzing (CI only)

A libFuzzer harness (`tests/fuzz/fuzz_hex_loader.cpp`) targets `mips::parse_hex_program` — the hex text parser that accepts untrusted input. It is **not** built by normal `cmake --preset debug` or `ctest` invocations. The `.github/workflows/cflite_pr.yml` workflow builds and runs it for 120 seconds via ClusterFuzzLite's base-builder image (Clang 22 + libFuzzer) — see [Contributing § CI workflows](Contributing#ci-workflows) for the exact trigger. To build it manually, pass `-DFUZZING_ENGINE=/path/to/libFuzzer.a` at configure time.

---

## Terminal requirements

The FTXUI terminal UI requires a real terminal with ANSI escape code support. It will not render correctly in:

- Windows `cmd.exe` without a compatibility layer
- Some minimal CI/CD environments

Use a standard Linux/macOS terminal, Windows Terminal, or any terminal that supports 256-color ANSI sequences.

---

## First steps

After launching `number_system_converter`, six tabs are available (navigate with **Tab**/**Shift+Tab** or the mouse):

1. **Converter** — type a number in any base; the others update live.
2. **CPU Dashboard** — load a program from Program Loader, then step through or auto-run.
3. **CPU Config** — switch between single-cycle and pipelined mode at runtime.
4. **Program Loader** — enter 32-bit instruction words in hex to load a program.
5. **Core Pulse** — an oscilloscope-style animation plus per-stage IF/ID/EX/MEM/WB detail.
6. **Utility** — placeholder tab reserved for future developer tools.

For the Qt6 GUI, write MIPS assembly directly in the Code Editor tab and click **Assemble & Load** — no separate toolchain needed.

See [Terminal UI](Terminal-UI) and [Qt6 GUI](Qt6-GUI) for full descriptions of each tab and its controls.
