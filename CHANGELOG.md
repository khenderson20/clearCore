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
- ClusterFuzzLite libFuzzer harness (`tests/fuzz/fuzz_hex_loader.cpp`) targeting `mips::parse_hex_program`;
  runs 120 s on every PR via `.github/workflows/cflite_pr.yml`. Addresses OpenSSF Scorecard fuzzing signal.
  `fuzz_hex_loader` CMake target is gated on `-DFUZZING_ENGINE=<engine>` — normal builds are unaffected.
  ClusterFuzzLite config lives in `.clusterfuzzlite/` (project.yaml, Dockerfile, build.sh). (#8, #9)

### Fixed
- Qt GUI: set each dock widget's `objectName` to its title for improved panel identification and debugging. (#7)

### Documentation
- Full README and wiki pass covering all changes introduced in PRs #2–#9: KSyntaxHighlighting, ClusterFuzzLite,
  security hardening, v0.0.1 release, and CI workflow table. Added release badge, CHANGELOG, and
  release-drafter automation.

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
