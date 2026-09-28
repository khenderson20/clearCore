# Contributing

## Workflow

1. Fork the repository
2. Create a feature branch from `main` with a descriptive name
3. Make your changes, ensuring existing tests still pass
4. Add new tests covering your changes
5. Run `ctest --preset debug` (or `ctest --preset asan`) to verify all tests pass
6. Open a pull request targeting the `main` branch

## CI workflows

This is the canonical list of what runs when — if you're checking a trigger condition, check here rather than README.md, which only summarizes.

CI (`.github/workflows/ci.yml`) runs four jobs on every push and PR to `main`: a `clang-format` check, a coverage build uploaded to Codecov (push events, or same-repo PRs — not fork PRs), a fast core-only test matrix (Debug and Debug+ASan/UBSan), and a full build exercising both Qt6 GUIs and Nyxstone (skipped for draft PRs). Every other workflow:

| Workflow                    | Trigger                                                        | Purpose                                                                                  |
|-----------------------------|-----------------------------------------------------------------|-------------------------------------------------------------------------------------------|
| `cross-platform.yml`        | Push/PR to `main` (`core-only` job); release published or manual dispatch (`build` job) | `core-only`: core build and tests on Windows x64 and macOS arm64. `build`: Windows NSIS installer and universal macOS `.dmg`, with Qt bundled |
| `codeql.yml`                | Push/PR to `main`, plus a weekly schedule                       | CodeQL static analysis for C++ and for the workflows themselves                          |
| `gitleaks.yml`              | Push/PR to `main`, or manual dispatch                           | Secret scanning; a CI backstop for the local gitleaks pre-commit hook                     |
| `zizmor.yml`                | Push to `main`; PRs that touch `.github/workflows/`            | Static analysis of the workflows (script injection, credential leakage, permissions)      |
| `dependency-review.yml`     | PRs that touch `.github/workflows/`                             | Flags known-vulnerable action versions (the dependency graph cannot parse CMake `FetchContent`) |
| `cflite_pr.yml`             | PRs that touch `src/`, `include/`, or `tests/fuzz/`             | 120 seconds of ClusterFuzzLite fuzzing on the harnesses the change reaches (see below)    |
| `cflite_batch.yml`          | Nightly, or manual dispatch                                     | One hour of fuzzing on both harnesses; grows the corpus on the `cifuzz-corpus` branch     |
| `cflite_prune.yml`          | Nightly, after the batch run, or manual dispatch                | Minimizes the `cifuzz-corpus` corpus                                                      |
| `cflite_cov.yml`            | Nightly, after the prune run, or manual dispatch                | Fuzzing coverage report, pushed to the `cifuzz-coverage` branch that `cflite_pr.yml` reads |
| `scorecard.yml`             | Push to `main`, plus a weekly schedule (**not** PR-triggered)   | Tracks OpenSSF supply-chain posture (token permissions, pinned actions, etc.)             |
| `release-drafter.yml`       | Push to `main`                                                  | Keeps the draft GitHub release current from merged PR titles                              |
| `release.yml`               | Release published, or manual dispatch                           | Linux `.tar.gz` via CPack, smoke-tested, with an SPDX SBOM                                 |
| `appimage.yml`              | Release published, or manual dispatch                           | Self-contained Linux AppImage, smoke-tested                                                |
| `update-changelog.yml`      | Release published                                               | Promotes `[Unreleased]` in `CHANGELOG.md` to the new version and opens a PR               |
| `wiki-sync.yml`             | Push to `main` that touches `wiki/` (**not** PR-triggered)      | Pushes `wiki/` to the GitHub wiki                                                          |

All third-party GitHub Actions in these workflows are pinned to full-length commit SHAs. Each job uses `step-security/harden-runner` to audit outbound network calls. Default `GITHUB_TOKEN` permissions are set to `read-only` at the repository level, with per-job overrides only where write access is needed.

---

## Code style

### Separation of concerns

`nsc_core` and `mips_core` must never include headers from `nsc_ui`, `nsc_qt`, or `nsc_quick`. This constraint is enforced via CMake target link dependencies. If you need a UI to know something about the core, expose it through `IProcessor` or `PipelineState` — do not reach back from the core into any UI layer.

### Type safety

Use `enum class` for hardware fields rather than plain integers. This prevents silent errors when passing opcode values where funct values are expected (or vice versa). Existing enums include `Opcode`, `FunctCode`, `InstrFormat`, and `StepResult` (CPU step outcome).

### Error handling

The core reports failure through return values, not exceptions — `std::optional`, `bool`, or a result type such as `StepResult`:

- `Decoder::decode()` returns `std::optional<DecodedInstr>` for unknown opcodes
- `isa::Memory` accessors are `noexcept`: reads return `std::nullopt` and writes return `false` for a misaligned or out-of-range address, rather than silently truncating
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

Target individual components (`parse_base`, ALU functions, individual decoder cases). Keep each test focused on a single behavior.

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

Two libFuzzer harnesses live in `tests/fuzz/`, covering the two functions that accept raw untrusted input:

| Harness                 | Target                     | Input                                                         |
|-------------------------|----------------------------|---------------------------------------------------------------|
| `fuzz_hex_loader.cpp`   | `mips::parse_hex_program`  | `.hex` program text, one word per line, as read by the TUI Program Loader and the Qt GUI's Open Program |
| `fuzz_elf_loader.cpp`   | `mips::parse_elf`          | Binary ELF32 — the only parser that sizes allocations and indexes from values in its own input |

Both are gated on `-DFUZZING_ENGINE=<engine>` at configure time and are never built by normal developer or CI builds.

`fuzz_elf_loader` has a seed corpus that `tests/fuzz/make_elf_corpus.py` generates. Fuzzing a binary format from an empty corpus burns most of the budget rediscovering the magic bytes and header layout, so the seeds cover valid executables, a pure `.bss` segment, `ET_REL`, truncated headers, and the crafted 4 GiB-allocation input that used to drive the parser out of memory. `build.sh` runs the script at build time and packs its output into `$OUT/fuzz_elf_loader_seed_corpus.zip`, which ClusterFuzzLite unpacks automatically. The seeds are not committed: OpenSSF Scorecard's Binary-Artifacts check counts every checked-in ELF file. To look at them, run `python3 tests/fuzz/make_elf_corpus.py`; it writes to `tests/fuzz/corpus/elf/`, which `.gitignore` excludes.

ClusterFuzzLite (`.clusterfuzzlite/`) provides the OSS-Fuzz-compatible project config, Dockerfile, and `build.sh`. `cflite_batch.yml` fuzzes both harnesses for an hour every night and grows the corpus on the `cifuzz-corpus` branch. `cflite_pr.yml` builds both on a pull request and spends 120 seconds on the ones the change reaches, using the coverage report on the `cifuzz-coverage` branch (triggers in [CI workflows](#ci-workflows) above). Crashes, hangs and out-of-memory conditions are reported as CI failures.

If you add a new function that parses untrusted text or binary input, add a parallel harness in `tests/fuzz/` following the same pattern — and remember that `.clusterfuzzlite/build.sh` names its targets explicitly, so a new CMake target alone is never built.

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
