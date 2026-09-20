#!/usr/bin/env python3
"""Regenerate the fuzz_elf_loader seed corpus.

Fuzzing an ELF parser from an empty corpus spends most of its budget
rediscovering the magic bytes and header layout, so the seeds below hand it a
structurally valid starting point plus the shapes that exercise the loader's
validation paths.  Run from the repo root:

    python3 tests/fuzz/make_elf_corpus.py
"""

import pathlib
import struct

OUT = pathlib.Path(__file__).resolve().parent / "corpus" / "elf"

EHDR_FMT = "<16sHHIIIIIHHHHHH"  # 52 bytes
PHDR_FMT = "<8I"  # 32 bytes
EHDR_SIZE = struct.calcsize(EHDR_FMT)
PHDR_SIZE = struct.calcsize(PHDR_FMT)
assert (EHDR_SIZE, PHDR_SIZE) == (52, 32)

IDENT = bytes([0x7F, ord("E"), ord("L"), ord("F"), 1, 1, 1]) + bytes(9)

ET_REL, ET_EXEC = 1, 2
EM_MIPS = 8
PT_LOAD = 1


def build(segments, entry=0, e_type=ET_EXEC):
    """segments: list of (vaddr, data, memsz). Returns the ELF file bytes."""
    data_off = EHDR_SIZE + PHDR_SIZE * len(segments)
    phdrs, blob = b"", b""
    for vaddr, content, memsz in segments:
        phdrs += struct.pack(
            PHDR_FMT, PT_LOAD, data_off + len(blob), vaddr, vaddr,
            len(content), memsz, 5, 4,
        )
        blob += content
    ehdr = struct.pack(
        EHDR_FMT, IDENT, e_type, EM_MIPS, 1, entry, EHDR_SIZE, 0, 0,
        EHDR_SIZE, PHDR_SIZE, len(segments), 0, 0, 0,
    )
    return ehdr + phdrs + blob


# `j self` — the emulator's halt idiom, so the seed is a runnable program.
HALT = struct.pack("<I", 0x0800_0000)
NOP = struct.pack("<I", 0x0000_0000)

SEEDS = {
    # Smallest valid image: one executable segment at address 0.
    "minimal_exec.elf": build([(0, NOP + HALT, 8)]),
    # Pure .bss (p_filesz == 0, p_memsz > 0) — the shape the loader used to
    # drop entirely, so it must stay in the corpus as a coverage anchor.
    "bss_only.elf": build([(0x1000, b"", 64)]),
    # Mixed segment with a zero-filled tail.
    "bss_tail.elf": build([(0x200, HALT, 32)]),
    # Two segments: text at 0, data at 0x400, plus a non-zero entry point.
    "two_segments.elf": build(
        [(0, NOP + HALT, 8), (0x400, b"\xde\xad\xbe\xef", 16)], entry=4
    ),
    # ET_REL is accepted (loaded at raw p_vaddr, unrelocated).
    "et_rel.elf": build([(0, HALT, 4)], e_type=ET_REL),
    # 88 bytes claiming a ~4 GiB segment. Before the p_filesz bound landed this
    # drove seg.data.resize() straight into libFuzzer's OOM limit; it is kept as
    # a coverage anchor for the rejection path.
    "oom_filesz_4gib.elf": build([(0, NOP, 4)])[:EHDR_SIZE + PHDR_SIZE + 4].replace(
        struct.pack("<II", 4, 4), struct.pack("<II", 0xFFFF_0000, 0xFFFF_0000), 1
    ),
    # Truncated before the program-header table ends.
    "truncated_phdr.elf": build([(0, NOP + HALT, 8)])[: EHDR_SIZE + 16],
    # Truncated inside the ELF header itself.
    "truncated_header.elf": build([(0, HALT, 4)])[:20],
}


# The OOM seed is built by byte-replacing a field pair, which is only unique
# because of the surrounding defaults in build(). Fail loudly if that stops
# being true rather than emitting a seed that no longer covers the bound.
assert struct.unpack("<8I", SEEDS["oom_filesz_4gib.elf"][52:84])[4] == 0xFFFF_0000


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for name, content in SEEDS.items():
        (OUT / name).write_bytes(content)
        print(f"{name}: {len(content)} bytes")


if __name__ == "__main__":
    main()
