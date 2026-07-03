# Contributing

## Workflow

1. Fork the repository
2. Create a feature branch from `develop` with a descriptive name
3. Make your changes, ensuring existing tests still pass
4. Add new tests covering your changes
5. Run `ctest --preset debug` (or `ctest --preset asan`) to verify all tests pass
6. Open a pull request targeting the `develop` branch

CI (`.github/workflows/ci.yml`) runs four jobs on every push and PR to `main` or `develop`: a `clang-format` check, a coverage build uploaded to Codecov, a fast core-only test matrix (Debug and Debug+ASan/UBSan), and a full build exercising both Qt6 GUIs and Nyxstone. Additional workflows run on every PR:

| Workflow                    | Purpose                                                                                  |
|-----------------------------|-------------------------------------------------------------------------------------------|
| `codeql.yml`                | CodeQL static analysis for C++                                                            |
| `dependency-review.yml`     | Blocks PRs that introduce dependencies with known vulnerabilities                         |
| `scorecard.yml`             | Tracks OpenSSF supply-chain posture (token permissions, pinned actions, etc.)             |
| `cflite_pr.yml`             | Runs 120 seconds of libFuzzer fuzzing via ClusterFuzzLite on `fuzz_hex_loader` (see below) |
| `wiki-sync.yml`             | Pushes `wiki/` to the GitHub wiki on merge to `main`                                     |

All third-party GitHub Actions in these workflows are pinned to full-length commit SHAs. Each job uses `step-security/harden-runner` to audit outbound network calls. Default `GITHUB_TOKEN` permissions are set to `read-only` at the repository level, with per-job overrides only where write access is needed.

---

## Code style

### Separation of concerns

`nsc_core` and `mips_core` must never include headers from `nsc_ui`, `nsc_qt`, or `nsc_quick`. This constraint is enforced via CMake target link dependencies. If you need a UI to know something about the core, expose it through `IProcessor` or `PipelineState` — do not reach back from the core into any UI layer.

### Type safety

Use `enum class` for hardware fields rather than plain integers. This prevents silent errors when passing opcode values where funct values are expected (or vice versa). Existing enums include `Opcode`, `FunctCode`, `InstrFormat`, and `StepResult` (CPU step outcome).

### Error handling

Use `std::optional` and exceptions for operations that can fail at runtime:

- `Decoder::decode()` returns `std::optional<Instruction>` for unknown opcodes
- Memory alignment errors throw rather than silently truncating
- The Qt6 assembler's `AssemblerResult` carries an `std::optional<std::string> error` rather than a sentinel value

Do not return sentinel integer values (`-1`, `0xDEADBEEF`) to signal errors in functions that could plausibly return those values legitimately.

### Const correctness

Apply `const` aggressively on member functions that do not mutate state, especially on `IProcessor` query methods. Apply `[[nodiscard]]` on functions whose return value must not be silently discarded — in particular, the ALU result, decoder output, and assembler result.

### No UI headers in core

A grep for `#include "ftxui/`, `#include <QWidget>`, or `#include <QtQml/`  in `src/mips/` or `src/nsc/` (the converter core) should return nothing. CI's core-only build path is one guard against this; consider adding a lint rule if you're touching that area.

---

## Testing conventions

Tests live in `tests/`, mirroring the `src/` structure:

```
tests/
  mips/
    decoder_test.cpp
    cpu_test.cpp
    processor_test.cpp    ← polymorphic: runs both backends
    disasm_test.cpp
  nsc/
    convert_test.cpp
  qt_ui/
    qt_ui_test.cpp         ← Qt6 assembler/controller/widget smoke tests
  golden/
    *.asm + golden_runner.cpp + run_golden.py   ← MARS differential tests
```

This project uses a **lightweight, dependency-free `CHECK()`-macro test harness** (see the top of any `tests/mips/*.cpp` file) — not GoogleTest or Catch2. Each `CHECK(expr)` reports pass/fail with file and line on failure; there's no fixture/assertion-framework API to learn.

### Unit tests

Target individual components (`parseBase`, ALU functions, individual decoder cases). Keep each test focused on a single behavior.

### Integration / polymorphic tests

`processor_test.cpp` runs the same programs through both `SingleCycleCpu` and `PipelinedCpu` and asserts identical final register and memory state. Any new CPU feature should be exercised through this harness so behavioral parity is maintained.

When adding a new instruction to the ISA:

1. Add a decoder case in `Decoder::decode()` (and to the Qt6 in-app assembler if it should be writable from the Code Editor)
2. Add an ALU or memory path in both `SingleCycleCpu` and `PipelinedCpu`
3. Add a test program in `processor_test.cpp` that exercises the instruction in isolation and in a sequence that would trigger hazards
4. Consider adding a `.asm` program under `tests/golden/` if the instruction is common enough to be worth a MARS cross-check

### Qt smoke tests

`qt_ui_test.cpp` (in `tests/qt_ui/`) exercises the `SimulatorController` signal/slot wiring, the assembler, and the widgets without a visible window (`QT_QPA_PLATFORM=offscreen`). Skipped when `BUILD_QT6_UI=OFF`. Keep these lightweight — they guard signal connectivity, not simulation correctness (that's `processor_test`'s job).

### Golden tests

`tests/golden/*.asm` programs are cross-checked against MARS (the classroom-standard reference MIPS simulator) via `golden_runner` and `run_golden.py`, for both CPU models. These require a JRE and Python 3 and are skipped automatically otherwise — don't assume they ran locally just because `ctest` reported success.

---

## Fuzzing

A libFuzzer harness lives at `tests/fuzz/fuzz_hex_loader.cpp`. It feeds arbitrary byte sequences to `mips::parse_hex_program`, which is the one function that accepts raw untrusted input (hex words from a file or the Program Loader tab). The harness is gated on `-DFUZZING_ENGINE=<engine>` at configure time and is never built by normal developer or CI builds.

ClusterFuzzLite (`.clusterfuzzlite/`) provides the OSS-Fuzz-compatible project config, Dockerfile, and `build.sh`. The `cflite_pr.yml` workflow builds and runs `fuzz_hex_loader` for 120 seconds on every PR. Crashes or hangs are reported as CI failures.

If you add a new function that parses untrusted text or binary input, consider adding a parallel harness in `tests/fuzz/` following the same pattern.

---

## CMake notes

- All new `.cpp` files must be added to the appropriate target in `CMakeLists.txt`. Forgetting this produces a linker error, not a compile error, so it can be confusing.
- Qt's MOC requires that `Q_OBJECT` classes be listed explicitly in CMake — they are not discovered automatically.
- FTXUI, GSL, spdlog, and (if enabled) Nyxstone are fetched by CMake at configure time (`FetchContent`). Do not vendor them manually. Nyxstone additionally needs a system LLVM+Clang in the **15–20** range; if your default LLVM is newer, point Nyxstone at an in-range install with the `NYXSTONE_LLVM_PREFIX` env var (see [Getting Started](Getting-Started#pinning-an-in-range-llvm-for-nyxstone)).
- Core code (`mips_core`) must not leak third-party headers through its own public headers: `NyxstoneBackend` pimpls away all LLVM/Nyxstone types, and the spdlog logger is reached only through `include/mips/trace.h`. When adding tracing, guard hot-path formatting behind `mips::trace_enabled(level)`.
- Prefer the CMake presets (`debug`, `release`, `asan`, `core-only`) over ad hoc configure invocations — they keep local builds, CI, and this wiki's instructions consistent.

---

## Commit messages

Follow the project's existing style (visible in `git log`):

- Imperative mood: "Add forwarding unit" not "Added forwarding unit"
- First line ≤ 72 characters
- Body (if needed) explains *why*, not *what* — the diff shows what changed

---

## Keeping the wiki and README in sync

**When you land a feature that changes what a user or contributor sees**, treat these as one update, not two separate ones:

1. Update `README.md` first — it's the single most-read entry point.
2. Update the matching wiki page(s). If you don't want to hand-edit the wiki through the GitHub web UI, see the sync workflow below.

A GitHub Actions workflow (`.github/workflows/wiki-sync.yml`) pushes the `wiki/` directory in this repository to the GitHub wiki automatically on merge to `main`, so wiki edits go through the same PR review as code — see the workflow file for setup (it needs a personal access token with wiki write access stored as a repository secret, since the default `GITHUB_TOKEN` can't push to the wiki repo).

---

## Opening issues

If you're unsure whether a change fits the project's direction, open an issue before writing code. Questions about code conventions are welcome — the architecture is intentionally academic, and the design decisions are worth discussing.
