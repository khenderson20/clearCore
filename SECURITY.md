# Security Policy

clearCore is an educational MIPS CPU simulator maintained by a single developer. This policy reflects that scope — it is not backed by a security team or formal SLA.

## Supported Versions

| Version | Supported |
| ------- | --------- |
| `main` (latest commit) | :white_check_mark: |
| Older commits / tags | :x: |

Only the current `main` branch receives security attention. There are no tagged releases yet.

## Reporting a Vulnerability

Please **do not** open a public GitHub issue for security vulnerabilities.

Preferred method: use GitHub's [private vulnerability reporting](https://github.com/khenderson20/clearCore/security/advisories/new) for this repository (Security tab → "Report a vulnerability"). This opens a private channel visible only to the maintainer until a fix is ready.

If you'd rather not use GitHub, open a regular issue asking to be contacted privately, without describing the vulnerability, and a contact method will be shared.

When reporting, include what's useful:
- What the issue is and where in the code it lives
- Steps to reproduce, if you have them
- Whether you've disclosed it anywhere else already

## What to Expect

This is a personal project, not a funded effort — response times depend on availability. There is no bug bounty program and no guaranteed turnaround, but reports will be acknowledged and taken seriously. Credit in the commit/advisory is offered by default; say if you'd rather stay anonymous.

## Scope

clearCore simulates MIPS instructions in software; it doesn't execute host machine code, doesn't touch the network, and isn't intended to run untrusted/adversarial input in production. Given that, the realistic security concerns are things like:
- Memory-safety bugs (buffer overflows, out-of-bounds access) in the C++ simulation core
- Crashes or hangs triggered by malformed assembly/binary input
- Known-vulnerable versions of dependencies (spdlog, Nyxstone, Qt6, FTXUI) pulled in via CMake

Simulation-accuracy bugs (an instruction behaving differently than real MIPS hardware) are correctness issues, not security issues — please file those as regular GitHub issues instead.
