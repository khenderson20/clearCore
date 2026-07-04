# Contributing to clearCore

Thanks for your interest in improving clearCore! Contributions of all kinds are
welcome — bug reports, documentation fixes, new tests, and features.

The full contributor guide lives in the wiki:
**[Contributing](https://github.com/khenderson20/clearCore/wiki/Contributing)**
(branching model, code style, testing conventions, and CI details).

## Quick start

1. Fork the repository.
2. Create a feature branch **from `develop`** (not `main`) with a descriptive name,
   e.g. `feat/branch-predictor` or `fix/decoder-signext`.
3. Make your changes, keeping core libraries (`nsc_core`, `mips_core`) free of any
   UI headers — see the [code style rules](https://github.com/khenderson20/clearCore/wiki/Contributing#code-style).
4. Add tests covering your change under `tests/`, mirroring the `src/` layout.
5. Verify locally:
   ```bash
   cmake --preset debug
   cmake --build --preset debug
   ctest --preset debug        # or: ctest --preset asan
   ```
6. Open a pull request **targeting `develop`**. Fill out the PR template so
   reviewers can see what changed and how it was verified.

## Before you open a PR

- Run `clang-format` (CI enforces it) — the project ships a `.clang-format`.
- Keep commits focused; a PR that does one thing is easier to review and land.
- If your change is user-facing, note it so it can be captured in the
  [CHANGELOG](CHANGELOG.md).

## Reporting bugs and requesting features

Use the [issue templates](https://github.com/khenderson20/clearCore/issues/new/choose)
(bug report / feature request). For security issues, follow
[SECURITY.md](SECURITY.md) instead of opening a public issue.

## Code of Conduct

Participation in this project is governed by our
[Code of Conduct](CODE_OF_CONDUCT.md). By taking part, you agree to uphold it.
