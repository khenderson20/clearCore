---
title: 'clearCore: An educational MIPS CPU simulator with live pipeline visualization'
tags:
  - C++
  - computer architecture
  - computer organization
  - CPU simulator
  - MIPS
  - pipeline
  - hazard detection
  - education
authors:
  - name: Kevin Henderson
    orcid: 0009-0002-8847-5459
    affiliation: 1
affiliations:
  - name: Independent Researcher, San Antonio, TX, USA
    index: 1
date: 4 July 2026
bibliography: paper.bib
---

# Summary

`clearCore` is a MIPS CPU simulator built for *seeing* how a processor
executes instructions. A student writes MIPS assembly in a built-in editor,
steps the processor cycle by cycle, and watches each instruction move through
the five classic pipeline stages — instruction fetch, decode, execute, memory
access, and write-back — with data hazards, pipeline stalls, control flushes,
and forwarding paths highlighted as they occur. Two execution engines, a
single-cycle datapath and a five-stage pipeline, implement a common
`IProcessor` interface and can be swapped at runtime without rebuilding, letting
learners contrast the two models on the same program. Beyond hand-typed
programs, `clearCore` loads little-endian MIPS ELF32 binaries, implements
Coprocessor 0 exception handling per the MIPS32r2 specification, and exposes a
GDB remote-serial-protocol stub so that a standard `gdb` client can attach to
the running emulator for breakpoints, single-stepping, and register inspection.

The simulator is written in C++20 and ships three interchangeable front ends
that all drive the same core libraries: a keyboard-driven terminal user
interface built with FTXUI [@ftxui], a Qt6 Widgets desktop GUI, and a Qt
Quick/QML GUI [@qt]. The processor and instruction-decoding logic live in
UI-independent libraries that are unit-tested in isolation, so pipeline behavior
is identical across every interface. The design follows the canonical
undergraduate computer-architecture texts by Patterson and Hennessy [@patterson]
and Harris and Harris [@harris].

# Statement of need

Pipelined execution — and the hazards, stalls, and forwarding it introduces — is
one of the most difficult concepts in an undergraduate computer-organization
course. Static datapath diagrams in textbooks show the *structure* of a pipeline
but not its *dynamics*: students struggle to connect a printed instruction ×
cycle diagram to the moment-to-moment movement of data through the datapath, and
in particular to why a stall or a forwarding path is required for a specific
sequence of instructions.

Existing educational simulators address parts of this gap. Ripes [@ripes]
provides a rich graphical datapath schematic but targets RISC-V; EduMIPS64
[@edumips64] and DrMIPS [@drmips] visualize MIPS execution through Java/Swing
GUIs; and QtMips [@qtmips] offers a Qt-based MIPS datapath view. `clearCore`
occupies a distinct point in this space: it pairs a modern C++20 core with
*multiple* front ends — including a fully keyboard-driven terminal UI that runs
over SSH with no graphical environment — and it emphasizes runtime-swappable
execution backends so that the same program can be observed under both a
single-cycle and a pipelined model. The terminal front end, in particular,
lowers the barrier to use in headless lab and remote-teaching settings where
installing a desktop GUI toolkit is impractical.

`clearCore` also goes beyond the pedagogical minimum by implementing hardware
behavior that is usually abstracted away in teaching simulators: real CP0
exceptions that vector rather than halting the machine, an ELF loader that
accepts binaries produced by an ordinary `mipsel` cross-toolchain, and a GDB
stub that lets students debug emulated programs with the same tool they use for
native code. These features let a single tool span an introductory
"what is a pipeline" lesson and a more advanced "how does exception handling and
debugging actually work" lesson.

The software is intended for undergraduate students learning computer
organization, instructors preparing lab exercises and lecture demonstrations,
and self-directed learners exploring processor internals. Its permissive MIT
license and unit-tested, UI-independent core also make it a reusable foundation
for coursework that extends the simulator — for example, adding branch
prediction or additional pipeline stages.

# Acknowledgements

We thank the maintainers of the open-source libraries `clearCore` builds upon,
in particular FTXUI and Qt.

# References
