# Changelog

All notable changes to clearCore are documented here.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
Versions follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

> **How this file stays current:** [release-drafter](https://github.com/release-drafter/release-drafter) collects
> merged PR titles into a draft GitHub Release on every push to `main`. When a release is published, the
> `update-changelog.yml` workflow promotes the `[Unreleased]` section to a versioned entry and commits back to
> `develop` automatically. Label PRs with the categories below so they land in the right section.
>
> | Label            | CHANGELOG section  |
> |------------------|--------------------|
> | `feature`        | Added              |
> | `enhancement`    | Changed            |
> | `bug`            | Fixed              |
> | `security`       | Security           |
> | `documentation`  | Documentation      |
> | `dependencies`   | Dependencies       |
> | `ci`             | CI / Internal      |

---

## [Unreleased]

### Fixed
- **Statistics for the single-cycle model**: Instructions and CPI read 0 in both Qt GUIs whenever
  the single-cycle CPU was selected, because retirement was counted from the WB slot that model
  never fills. Retirement is now a backend-defined `PipelineState::retired` flag shared by every
  front end (the TUI's CPI gauge uses it too).
- **Branches and jumps now travel through MEM and WB** in the pipelined model instead of vanishing
  after EX, so the Pipeline Trace grid shows all five stages for them and they count as retired.
- **MEM and WB stage labels** no longer read "nop" for every instruction: the pipeline registers
  now carry the machine word into those stages.
- **Exceptions are visible**: a new `exceptionRaised` signal names the trap and its EPC in the
  Widgets status bar and Pipeline Events log, the QML status pill, and the TUI status line. A
  nested exception while `Status.EXL` is set (e.g. the vector being unmapped in a small address
  space) no longer overwrites EPC, matching the MIPS32 PRA.
- Hex program listings now parse identically on every platform. `parse_hex_program` used
  `std::stoul`, whose `unsigned long` is 64-bit on Linux/macOS but 32-bit on Windows, so a token
  wider than 32 bits was silently truncated and accepted on LP64 and rejected on Windows. Replaced
  with `std::from_chars` into a `uint32_t`, which rejects overflow everywhere and reports failure
  by return value rather than by exception.

### Documentation
- The three diagrams that are genuinely graphs — the Architecture module overview, the Qt6
  `SimulatorController` signal flow, and the `ci.yml` job map — are now Mermaid rather than hand-drawn
  box art, which GitHub renders natively in both wikis and repo files. Colour carries meaning rather
  than decoration: the `isa::` contract, the `mips::` backend, the UI layer and `nsc_core` each get
  their own, so the layer rule CLAUDE.md calls hard is visible instead of only stated. Dark fills with
  light strokes keep them legible under both GitHub themes. The pipelined-CPU instruction × cycle
  chart stays ASCII deliberately — it is a fixed grid, which Mermaid renders worse than a table.
- Wiki diagrams re-synced with the code they describe. The Qt6 GUI page documented a worker-thread
  model that does not exist — there is no `QThread` or `moveToThread` anywhere in the Qt layer, and
  the repo's own `src/nsc_qt/docs/SimulatorController.md` already said so; the page now describes the
  zero-interval `QTimer` on the GUI thread, the 5000-cycle update throttle, and `exceptionRaised`.
  The Architecture module diagram had the `nsc_core` dependency arrow backwards (only the TUI links
  it; neither Qt GUI does) and its box edges did not line up. The pipelined-CPU diagram drew two
  identical cycle rows and is now a proper instruction × cycle chart. CLAUDE.md's `ci.yml` job map
  still said coverage was push-only and full-build was PR-only.
- Roadmap, README and CLAUDE.md now point at the GitHub milestones that track each stage, and two
  stale claims are corrected: the Qt6 assembler was described as single-pass with backpatched labels
  when it has been two-pass since it shipped, and the TUI was described as lacking a performance
  panel when it has had a live `Telemetry` panel all along. CLAUDE.md also records why `Closes #N`
  never fires on this repo. (#183)
- CLAUDE.md's Codecov section no longer ends with a four-step "to complete your setup" checklist
  for work that is already done, one step of which pointed at a `ci.yml` condition that no longer
  exists. Replaced with a factual description of the `coverage` job, the gates and `ignore` list in
  `codecov.yml`, and why the Qt front ends are out of scope. (#174)

### CI / Internal
- **The TUI tab-count invariant is enforced by the compiler, not a comment.** `Container::Tab`
  selects `children()[*selector % children().size()]`, so a child list shorter than the label list
  makes high tab indices alias onto an earlier tab's live components — tab 4 wrapped to the
  Converter and tab 5 to the CPU controls, where Enter could fire Run/Reset. The labels are now a
  `constexpr` array and the child list's size is deduced from its own initialiser, so a
  `static_assert` fails the build if either list grows without the other. The message names the
  aliasing bug and says to add a placeholder container. (#195)
- `update-changelog.yml` also aligns `wiki/Home.md`'s version, which the previous pass missed — it
  read `v0.1.0` against a released 0.3.5. The pattern is anchored on `MIT license · v` so it cannot
  match the dependency versions on the next line.
- `update-changelog.yml` now aligns every version-bearing file with the release tag, not just the
  CHANGELOG: `CITATION.cff`'s `version` and `date-released` and the README BibTeX `version` move
  too, in the same PR. Nothing had ever updated those two, so at v0.3.5 the citation metadata said
  0.3.4 and the README BibTeX said 0.1.0. The Zenodo `doi:` is deliberately left alone — it is the
  concept DOI, which resolves to the latest version and is version-independent by design. Each
  substitution fails the job if its target is missing, so the drift cannot quietly return. (#211)
- MSVC builds now compile with `/permissive-` alongside `/W4`, enabling two-phase name lookup and
  the rest of MSVC's conformance checking — the divergence the Windows CI leg documents itself as
  catching but previously did not.
- Dropped the `macos-x86_64` leg from `cross-platform.yml`'s pre-merge `core-only` matrix. It ran on
  the `macos-13` image, which GitHub retired on 2025-12-08, so the job queued forever and left every
  PR showing a permanently-pending check (`timeout-minutes` bounds execution, not queue time). Not
  replaced with `macos-15-intel`: the release `build` job already dropped its Intel runner because
  Qt's macOS binaries are universal, and an Intel macOS leg adds only AppleClang + libc++ on x86_64
  over the remaining `macos-14` and Linux legs. (#169)
- Split the hex program loader's tests out of `tests/mips/disasm_test.cpp` into
  `tests/mips/program_loader_test.cpp`, matching the one-test-file-per-module convention the rest
  of `tests/mips/` follows. A loader regression reported as `disasm_test` failing, which points at
  the wrong translation unit, and `ctest -R program_loader` selected nothing. Pure move: all 42
  assertions preserved — 15 in the disassembler file, 27 in the new one. (#172)
- `ENABLE_SANITIZERS=ON` no longer emits GNU-style `-fsanitize` flags under MSVC, where `cl.exe`
  does not accept them and the build silently comes out uninstrumented. The MSVC branch uses
  `/fsanitize=address`; UBSan has no MSVC equivalent, so that branch is ASan-only. The GCC/Clang
  path is unchanged, so the `asan` CI leg is unaffected. (#171)
- Added a repo-root `.clangd` pointing at `build/debug`. clangd searches a source file's own
  directory and its parents for `compile_commands.json`; `build/<preset>/` is neither, so every
  translation unit was parsed standalone with no include paths — real headers reported "file not
  found", core types reported "unknown type name", and cross-file navigation returned nothing.
  (#170)

---

## [0.3.5] - 2026-07-13

---

## [0.3.4] - 2026-07-09


---

## [0.3.2] - 2026-07-09

---

## [0.3.1] - 2026-07-05

---

## [0.3.0] - 2026-07-05

### Added
- **Ripes-style schematic datapath**: the Datapath panel is now a full circuit schematic of the 5-stage
  pipeline (QGraphicsScene) — PC, instruction memory, register file, control, sign-extend, forwarding muxes,
  ALU, branch logic, data memory, and the four pipeline registers, connected by routed wires. Live per cycle:
  forwarding buses light orange/purple, the branch-flush path lights red, the write-back loop lights green
  (only when the retiring instruction actually writes a register), mux select-input dots show the chosen input,
  mnemonic + PC labels sit above each stage column, and value labels pin the fetch PC, ID immediate, and WB
  result to their wires. Educational hover tooltips on every unit explain what it does and what it is doing
  this cycle (control-signal breakdown included). Step animation glides an instruction token between columns
  each clock edge. A hazard explainer chip names each stall/flush in plain language the moment it happens.
  Ctrl+wheel / overlay buttons zoom; right-click exports a 2x PNG (`datapath-cycle-N.png`). (#83, #84)
- **Pipeline Events panel**: scrolling, colour-coded log of every forward, stall, and flush (with the
  instruction responsible), plus program milestones (load, breakpoints, halt, fault). Consecutive-cycle
  repeats collapse into one entry; colours match the schematic wires. (#84)
- **2-column IDE layout** (Qt Advanced Docking System): Code Editor left; Datapath centre; Registers, Memory,
  Pipeline Trace, Statistics, and Pipeline Events tabbed below. Panels dock, float, and persist; View ▸ Panels
  toggles and View ▸ Reset Layout restore them. (#80, #82)
- **Statistics redesign**: Cycles / Instructions / CPI as large KPI cards, with the CPI card colour-coded
  green/orange/red by pipeline health; pipeline-event counters gained explanatory tooltips. (#82)
- Execution-speed slider in the main toolbar (10–1000 ms per cycle), synced with Preferences. (#84)

### Fixed
- **TUI froze on a blank screen at launch in packaged Linux builds** (`number_system_converter` from the
  release archive): signed-integer-overflow UB in the startup splash's coordinate hash let GCC `-O3`
  miscompile the splash setup into an infinite loop. Hash arithmetic now uses unsigned math; `-O0`/`-O2`
  builds were never affected, which is why local builds ran fine. (#87)
- Light/dark theming: ADS dock chrome no longer inherits the system palette (dark tab bars in light mode),
  dropdown/spinbox arrows render again, dark-mode schematic stage tints no longer drown the wiring, and the
  memory hex view labels its Offset/ASCII columns. (#82, #84)

---

## [0.2.1] - 2026-07-05

---

## [0.2.0] - 2026-07-05

### Added
- **Windows and macOS release assets**: published releases now attach a Windows NSIS installer
  (`clearCore-<ver>-windows-x64.exe`) and a universal macOS `.dmg` (`clearCore-<ver>-macOS-universal.dmg`,
  arm64 + x86_64), each with the Qt runtime bundled, alongside the existing Linux `.tar.gz`/AppImage. Built by a
  new cross-platform workflow. Installers are currently unsigned (see the README Download section).

### Documentation
- README and `CITATION.cff` reframed as a multi-ISA CPU-architecture simulator that foreshadows the RISC-V
  backend (was described as MIPS-only). (#61)
- README gains a Download section listing per-platform assets and how to clear the Gatekeeper/SmartScreen
  warnings on the unsigned builds.

### CI / Internal
- `update-changelog.yml` now opens a pull request into `develop` instead of pushing directly, so the CHANGELOG
  promotion is no longer rejected by the `develop` branch ruleset (`GH013: Changes must be made through a pull
  request`).
- Added `release-pr.yml`: keeps a standing `develop → main` release-promotion PR open automatically on each push
  to `develop`, so cutting a release is a single merge.

---

## [0.1.1] - 2026-07-04

Internal refactor release: carves the ISA-agnostic `isa::` core out of the MIPS backend so a future RISC-V (RV32I)
backend can reuse the memory, register file, pipeline state, and processor interface. No user-facing behavior change.

### Changed
- Split the simulation core into an ISA-agnostic `isa::` layer: `Memory`, `RegisterFile`, and `IProcessor` (with
  `PipelineState`/`StepResult`/`StageSnapshot`) moved to `include/isa/`; `IProcessor` split into `isa::IProcessor`
  (agnostic) + `mips::IMipsProcessor` (adds CP0, HI/LO, and the MIPS `Control` word). `mips::` `using`-shims keep
  every existing caller and all three UIs unchanged. (#57)

### Documentation
- Wiki (Architecture, Roadmap, GDB-Stub) documents the `isa::` core split and the phased RISC-V roadmap. (#59)

### CI / Internal
- Fixed `update-changelog.yml`: corrected the `harden-runner` commit SHA (previously unresolvable, which failed the
  job at startup) and rewrote the malformed promote step.

---

## [0.1.0] - 2026-07-04

First feature release since v0.0.1 — clearCore becomes citable and downloadable. Adds packaging (CPack,
`.deb`/`.rpm`), a self-contained AppImage of the Qt6 GUI, release automation, and full citation/community metadata.

### Added
- Self-contained **AppImage** of the Qt6 Widgets GUI — bundles Qt6 and the Qt Advanced Docking System so it runs
  with no toolchain or Qt install; built and Xvfb-smoke-tested via `.github/workflows/appimage.yml`. (#43)
- Release workflow (`.github/workflows/release.yml`): on a published release, builds, tests, and attaches a
  `clearCore-<version>-Linux-x86_64.tar.gz`. (#42)
- `install()` rules and CPack packaging (`.tar.gz`/`.deb`/`.rpm`) with a clean install tree that excludes
  dependency headers and CMake files. (#41)
- `CITATION.cff` enabling GitHub's "Cite this repository" button, with a slot for a Zenodo DOI. (#39)
- JOSS paper draft under `paper/`. (#40)
- `CODE_OF_CONDUCT.md`, a root `CONTRIBUTING.md`, and a pull-request template — community health to 100%. (#39)
- Issue templates (bug report / feature request / custom) and a Sponsor button (`FUNDING.yml`). (#35, #36, #38)
- ClusterFuzzLite libFuzzer harness (`tests/fuzz/fuzz_hex_loader.cpp`) targeting `mips::parse_hex_program`;
  runs 120 s on every PR via `.github/workflows/cflite_pr.yml`. Addresses OpenSSF Scorecard fuzzing signal.
  `fuzz_hex_loader` CMake target is gated on `-DFUZZING_ENGINE=<engine>` — normal builds are unaffected. (#8, #9)

### Changed
- Version set to `0.1.0` across `CMakeLists.txt`, `CITATION.cff`, and the wiki. (#47)

### Fixed
- Qt GUI: set each dock widget's `objectName` to its title for improved panel identification and debugging. (#7)

### Documentation
- Restructured the README for new-developer readability. (#25)
- Documented the Qt Quick/QML GUI and Nyxstone; corrected the test-suite count. (#21)
- Full README and wiki pass covering PRs #2–#9 (KSyntaxHighlighting, ClusterFuzzLite, security hardening, the
  v0.0.1 release, and the CI workflow table); added the release badge and release-drafter automation.

### CI / Internal
- Reduced PR job count with path filters and conditional jobs. (#23)
- Tightened the changelog workflow's top-level token permissions to `read-all`. (#27)

---

## [0.0.1] - 2026-07-02

First stable release. Establishes the full Qt6 desktop GUI alongside the FTXUI terminal UI, the dual-backend
simulator core, MARS differential testing, coverage CI, and supply-chain hardening.

### Added
- Optional MIPS assembly syntax highlighting in the Qt6 Code Editor via
  [KSyntaxHighlighting](https://invent.kde.org/frameworks/syntax-highlighting) (KDE framework, MIT since KF 5.50).
  Attaches Kate's MIPS Assembler definition (GNU Assembler fallback) when the system package is found at configure
  time; tracks the application light/dark theme preference. No impact on builds without the package. (#4)
- `.github/CODEOWNERS` file defining default code ownership. (#5)

### Changed
- Bumped project version to `0.0.1` in `CMakeLists.txt`. (#6)
- Extended CI workflow triggers to include the `develop` branch alongside `main`. (#6)

### Fixed
- `CDockWidget` construction updated to pass the dock manager as the first argument (deprecated signature removed). (#6)
- Added missing `<string>` and `<utility>` includes to `src/nsc_qt/main_window.cpp` for cleaner builds. (#6)

### Security
- Pinned all third-party GitHub Actions to full-length commit SHAs across `ci.yml`, `codeql.yml`, and
  `scorecard.yml`. (#3)
- Added `step-security/harden-runner` to all CI jobs to audit outbound network calls and prevent credential
  exfiltration. (#3)
- Set default `GITHUB_TOKEN` permissions to `read-only` at repository level; per-job write overrides only where
  required. (#3)
- Added `dependency-review.yml` workflow: blocks PRs that introduce dependencies with known CVEs. (#3)
- Extended `.pre-commit-config.yaml` with whitespace, YAML, secret-scanning, C++ linting, and Python linting
  hooks. (#3)

### Dependencies
- Bumped `actions/checkout` v4 → v7. (#2)
- Bumped `github/codeql-action` v3 → v4. (#2)
