#!/usr/bin/env python3
"""MARS golden test driver.

Assembles a .asm program with the MARS reference simulator (memory layout
CompactTextAtZero: .text at 0x0000, .data at 0x2000), runs it in MARS to get
the golden register state, runs the identical machine words on a clearCore
CPU via golden_runner, and diffs the two register files.

MARS programs halt with a self-targeting jump: clearCore retires it as Halt,
while MARS spins until the step limit — the register state is stable either
way, so the comparison is exact.

Invoked by ctest; see the golden-test section of CMakeLists.txt.
"""

import argparse
import pathlib
import re
import subprocess
import sys
import tempfile

# Registers with program-defined values. $sp/$gp/$fp/$k0/$k1 are excluded:
# MARS initialises them from its OS conventions, clearCore zeroes them.
REGS = (
    "$at $v0 $v1 $a0 $a1 $a2 $a3 "
    "$t0 $t1 $t2 $t3 $t4 $t5 $t6 $t7 $t8 $t9 "
    "$s0 $s1 $s2 $s3 $s4 $s5 $s6 $s7 $ra"
).split()

STEP_LIMIT = 10_000  # ample for every corpus program

REG_LINE = re.compile(r"^(\$\w+)\t(0x[0-9a-f]{8})$")


def run(cmd: list[str], what: str) -> str:
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        sys.exit(f"FAIL ({what}, exit {proc.returncode}):\n"
                 f"  cmd: {' '.join(map(str, cmd))}\n{proc.stdout}{proc.stderr}")
    return proc.stdout


def parse_regs(output: str, what: str) -> dict[str, str]:
    regs = {m.group(1): m.group(2) for line in output.splitlines()
            if (m := REG_LINE.match(line.strip()))}
    missing = [r for r in REGS if r not in regs]
    if missing:
        sys.exit(f"FAIL ({what}): no value reported for {' '.join(missing)}\n{output}")
    return regs


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--java", required=True)
    ap.add_argument("--mars-jar", required=True)
    ap.add_argument("--runner", required=True)
    ap.add_argument("--cpu", required=True, choices=["single", "pipelined"])
    ap.add_argument("asm", type=pathlib.Path)
    args = ap.parse_args()

    mars = [args.java, "-jar", args.mars_jar, "nc", "mc", "CompactTextAtZero"]

    with tempfile.TemporaryDirectory() as td:
        text_hex = pathlib.Path(td) / "text.hex"
        data_hex = pathlib.Path(td) / "data.hex"

        run(mars + ["a", "ae1", "dump", ".text", "HexText", str(text_hex), str(args.asm)],
            "MARS assemble")
        # Programs without a .data segment make this dump fail; that's fine.
        subprocess.run(mars + ["a", "dump", ".data", "HexText", str(data_hex), str(args.asm)],
                       capture_output=True)

        golden = parse_regs(
            run(mars + ["ae1", "se1", str(STEP_LIMIT), "hex", *REGS, str(args.asm)],
                "MARS run"),
            "MARS run")

        runner_cmd = [args.runner, args.cpu, str(text_hex)]
        if data_hex.exists():
            runner_cmd.append(str(data_hex))
        actual = parse_regs(run(runner_cmd, "golden_runner"), "golden_runner")

    diffs = [(r, golden[r], actual[r]) for r in REGS if golden[r] != actual[r]]
    if diffs:
        print(f"FAIL: {args.asm.name} on {args.cpu} disagrees with MARS:")
        print(f"  {'reg':<5} {'MARS':<12} clearCore")
        for reg, want, got in diffs:
            print(f"  {reg:<5} {want:<12} {got}")
        sys.exit(1)
    print(f"OK: {args.asm.name} on {args.cpu} matches MARS on {len(REGS)} registers")


if __name__ == "__main__":
    main()
