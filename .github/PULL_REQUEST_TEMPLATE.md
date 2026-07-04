<!--
Thanks for contributing to clearCore!
PRs should target the `develop` branch. See CONTRIBUTING.md for the full guide.
-->

## Summary

<!-- What does this PR change, and why? -->

## Related issues

<!-- e.g. "Closes #123" or "Refs #123". Delete if none. -->

## Type of change

- [ ] Bug fix (non-breaking change that fixes an issue)
- [ ] New feature (non-breaking change that adds functionality)
- [ ] Breaking change (fix or feature that changes existing behavior)
- [ ] Documentation / wiki
- [ ] Build / CI / tooling

## How was this verified?

<!-- Commands run and results. -->

- [ ] `cmake --build --preset debug` succeeds
- [ ] `ctest --preset debug` passes
- [ ] `ctest --preset asan` passes (if the change touches core logic)
- [ ] Added or updated tests under `tests/`

## Checklist

- [ ] Branch is based on and targets `develop`
- [ ] Code is `clang-format`-clean
- [ ] Core libraries (`nsc_core`, `mips_core`) include no UI headers
- [ ] Docs / CHANGELOG updated if the change is user-facing
