# Terminal UI

The terminal interface (`number_system_converter`) is built with [FTXUI v7.0.0](https://github.com/ArthurSonzogni/FTXUI) and runs entirely in the terminal with no system dependencies beyond a C++20 compiler and CMake.

---

## Launch

```bash
./cmake-build-debug/number_system_converter
```

Requires a terminal with ANSI escape code support (256 colors recommended). Does not work in bare `cmd.exe` on Windows without a compatibility layer.

---

## Tab overview

Navigate tabs with **Tab** / **Shift+Tab** or the mouse. There are six tabs.

### Tab 0 — Converter

Real-time base conversion. Type a value in any field; the other two update instantly.

- **Binary** — unsigned 64-bit binary string
- **Hexadecimal** — `0x`-prefixed or bare hex digits
- **Decimal** — unsigned decimal

Input is validated on each keystroke. Invalid characters are rejected without clearing the field. Internally all three representations share a single `uint64_t`.

---

### Tab 1 — CPU Dashboard

The main CPU visualization. From top to bottom:

**Datapath panel** — current PC, raw instruction word, cycle counter, and CPU mode (single-cycle or pipelined). Shows a snapshot of control signals at each stage: IF fetch address, ID register reads, EX ALU operation, MEM access type, WB destination.

**Pipeline strip** — a horizontal row of five labeled boxes (IF · ID · EX · MEM · WB), each showing the instruction currently in that stage. Color coding:

| Color  | Meaning                |
|--------|--------------------------|
| Green  | Normal advance            |
| Yellow | Stall / bubble             |
| Red    | Flushed (branch/jump)      |
| Cyan   | Forwarding path active      |

**Hazard & forwarding panel** — text breakdown of which hazard conditions fired this cycle, which forwarding paths are active, and which registers are involved.

**Register file panel** — all 32 MIPS registers in a two-column grid. Shows both the ABI name (`$t0`, `$sp`, etc.) and the register number. The last-written register is highlighted. Values are shown in hexadecimal with a signed-decimal annotation.

**Execution trace** — last 8 committed instructions in order (WB stage sourced), scrollable.

**Telemetry panel** — live gauges for cycle count, stalls, forwards, flushes, and current CPI.

**Execution controls** — these are clickable/focusable widgets, not global keyboard shortcuts (navigate to them with **Tab**, then press **Enter** or click):

| Control          | Action                                       |
|--------------------|-------------------------------------------------|
| **Step** button     | Advance one cycle                                |
| **Run/Pause** toggle| Start or stop auto-run                            |
| **Run→Halt** button | Auto-run until `StepResult::Halt`                 |
| **Reset** button    | Reset CPU state and telemetry (program preserved) |
| Speed slider        | 10 ms/cycle (fastest) to 1000 ms/cycle (slowest)  |

The one dedicated keyboard shortcut on this tab is **F10**, which single-steps the CPU regardless of which widget has focus. **PageUp** / **PageDown** / **Home** scroll the memory panel (Home jumps to the row containing the current PC). The pipeline flow canvas animates only while auto-run is active.

---

### Tab 2 — CPU Config

Switch the active CPU backend without restarting:

- **Single-cycle** — one instruction per clock, no pipeline registers
- **Pipelined** — full 5-stage pipeline with hazard handling

Switching resets the CPU state and clears telemetry counters. The loaded program is preserved.

---

### Tab 3 — Program Loader

Enter MIPS instruction words as 32-bit hex values, one per line:

```
00401000
00602020
8C620000
AC620004
08000006
```

Click **Load** to transfer the program to the CPU. The memory panel in Tab 1 updates immediately. The CPU is reset to PC = 0 before the new program runs.

---

### Tab 4 — Core Pulse

An oscilloscope-style animation — layered sine waves sweep across a graticule grid, phase-shifting to look like a live scope trace — plus a per-stage IF/ID/EX/MEM/WB instruction and control-signal breakdown panel. The waveform animates continuously; it intensifies while auto-run is active. Mouse movement inside this panel is tracked for an interactive cursor readout.

The startup splash animation (same visual style) plays before the CPU is first stepped.

---

### Tab 5 — Utility

A placeholder tab reserved for future developer tooling (profiling data viewers, network simulation interfaces, etc.). Currently displays a "Feature development pending" message — no functionality yet. See [Roadmap](Roadmap).

---

## Known terminal quirks

- **Emoji in layout-critical positions** cause border misalignment in some terminals. FTXUI's `IsFullWidth()` table does not classify most emoji as double-width, so they consume one column visually but the layout engine allocates two. The UI avoids emoji in tab bars and headers for this reason.

- **Tab focus routing** — FTXUI's `Container::Tab` wraps focus index modulo the child count. An earlier bug caused tabs 4–5 to alias onto tabs 0–1's interactive components. This was fixed by ensuring the container child count exactly matches `tab_labels.size()` (six entries) at construction. A regression guard is planned — see [Roadmap](Roadmap).
