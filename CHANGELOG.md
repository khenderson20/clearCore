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
