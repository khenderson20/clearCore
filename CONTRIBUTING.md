# Contributing to clearCore

Thanks for your interest in improving clearCore! Contributions of all kinds are
welcome — bug reports, documentation fixes, new tests, and features.

The full contributor guide lives in the wiki:
**[Contributing](https://github.com/khenderson20/clearCore/wiki/Contributing)**
(branching model, code style, testing conventions, and CI details).

## Quick start

1. Fork the repository.
2. Create a feature branch **from `main`** with a descriptive name,
   e.g. `feat/branch-predictor` or `fix/decoder-signext`.
3. Make your changes, keeping core libraries (`nsc_core`, `mips_core`) free of any
   UI headers — see the [code style rules](https://github.com/khenderson20/clearCore/wiki/Contributing#code-style).
4. Add tests covering your change under `tests/`, mirroring the `src/` layout.
5. Verify locally:
   ```bash
   cmake --preset core-only
   cmake --build --preset core-only
   ctest --preset core-only
   ```
   `core-only` builds in about 90 seconds with nothing but a compiler and CMake.
   If your change touches a Qt GUI, use `debug` instead (needs Qt6 and, for
   Nyxstone, LLVM 15–20 — see the dependency list in `CMakeLists.txt`'s header
   comment):
   ```bash
   cmake --preset debug
   cmake --build --preset debug
   ctest --preset debug        # or: ctest --preset asan
   ```
6. Open a pull request **targeting `main`**. Fill out the PR template so
   reviewers can see what changed and how it was verified.

## Before you open a PR

- Run `clang-format` (CI enforces it) — the project ships a `.clang-format`.
- Keep commits focused; a PR that does one thing is easier to review and land.
- If your change is user-facing, note it so it can be captured in the
  [CHANGELOG](CHANGELOG.md).

## Reporting bugs and requesting features

Use the [issue forms](https://github.com/khenderson20/clearCore/issues/new/choose):
**Bug report** for a defect, **Feature request** for a new capability, and
**Quality or maintenance task** for a refactor, a performance problem, a test or
documentation gap, or a CI change. For security issues, follow
[SECURITY.md](SECURITY.md) instead of opening a public issue.

The forms and the issues are written in
[ASD-STE100 Simplified Technical English](https://www.asd-ste100.org/): short
sentences (20 words at most for an instruction), active voice, one instruction
in each sentence, and approved words ("make sure", not "ensure"). Code
identifiers, file paths and tool names stay as they are.

## Code of Conduct

Participation in this project is governed by our
[Code of Conduct](CODE_OF_CONDUCT.md). By taking part, you agree to uphold it.
