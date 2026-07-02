#include "nsc/ui.h"
#include "nsc/converter.h"
#include "mips/processor.h"
#include "mips/single_cycle_cpu.h"
#include "mips/pipelined_cpu.h"
#include "mips/decoder.h"
#include "mips/disassembler.h"
#include "mips/program_loader.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/canvas.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/terminal.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <deque>
#include <format>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace nsc {

using namespace ftxui;

// ─── CPU mode ─────────────────────────────────────────────────────────────────
enum class CpuMode { SingleCycle, Pipelined };

// ─── MIPS ABI register names ──────────────────────────────────────────────────
static constexpr std::array<std::string_view, 32> kRegNames = {
    "zero","at","v0","v1","a0","a1","a2","a3",
    "t0",  "t1","t2","t3","t4","t5","t6","t7",
    "s0",  "s1","s2","s3","s4","s5","s6","s7",
    "t8",  "t9","k0","k1","gp","sp","fp","ra"
};

// ─── Execution trace entry ─────────────────────────────────────────────────────
struct TraceEntry { uint32_t pc; uint32_t raw; };

// ─── Stage accent colour by index + state ─────────────────────────────────────
static Color stage_accent(int idx, const mips::StageSnapshot& s) {
    if (!s.valid)  return Color::GrayDark;
    if (s.flushed) return Color::Red;
    if (s.stalled) return Color::Yellow;
    switch (idx) {
        case 0: return Color::BlueLight;
        case 1: return Color::MagentaLight;
        case 2: return Color::YellowLight;
        case 3: return Color::GreenLight;
        default: return Color::CyanLight;
    }
}

// ─── Hex file loader ──────────────────────────────────────────────────────────
// Thin UI adapter over mips::load_hex_file (parsing logic lives in mips_core,
// where it is unit-tested). This layer only wires the parsed words into the
// processor and formats a status line for the TUI.
static bool load_hex_file(const std::string& path, mips::IProcessor& cpu,
                          std::string& msg) {
    const mips::HexProgram prog = mips::load_hex_file(path);
    if (!prog.ok()) { msg = *prog.error; return false; }
    if (cpu.load_program(prog.words, 0)) {
        msg = std::format("Loaded {} words.", prog.words.size());
        return true;
    }
    msg = "Program too large for memory.";
    return false;
}

// ─── Assembly mnemonic reconstruction ─────────────────────────────────────────
// Thin UI adapter over mips::Disassembler (logic lives in mips_core and is
// unit-tested in tests/mips/disasm_test.cpp).
static std::string reconstruct_asm(const mips::DecodedInstr& dec, uint32_t pc) {
    return mips::Disassembler::to_string(dec, pc);
}

// ─── Pipeline stage box ───────────────────────────────────────────────────────
// D: stalled stage keeps its mnemonic but colours it amber (not "STALL" text).
static Element make_stage_box(const mips::StageSnapshot& snap, int idx) {
    std::string mnemonic = "─────";
    if (snap.valid) {
        if (snap.raw != 0) {
            if (auto d = mips::Decoder::decode(snap.raw))
                mnemonic = std::string(mips::Decoder::mnemonic(*d));
            else mnemonic = "???";
        } else {
            mnemonic = "(nop)";
        }
    }

    Color accent    = stage_accent(idx, snap);

    // D: mnemonic colour — amber for stall, red for flush, white for valid, dim for empty
    Color mnem_col;
    if (!snap.valid)       mnem_col = Color::GrayDark;
    else if (snap.stalled) mnem_col = Color::Yellow;
    else if (snap.flushed) mnem_col = Color::Red;
    else                   mnem_col = Color::White;

    std::string status;
    if (snap.stalled)      status = "STALL";
    else if (snap.flushed) status = "FLUSH";
    else if (snap.valid)   status = std::format("@{:07X}", snap.pc);
    else                   status = "       ";

    Elements inner;
    inner.push_back(text(mnemonic) | center | color(mnem_col) | (snap.valid ? bold : dim));
    inner.push_back(text(status)   | center | dim | color(accent));

    Element box = window(
        text(std::string(snap.name)) | bold | color(accent),
        vbox(inner)
    ) | size(WIDTH, GREATER_THAN, 10);
    if (!snap.valid) box = box | dim;
    return box;
}

// ─── Full pipeline visualization ──────────────────────────────────────────────
static Element render_pipeline(const mips::PipelineState& ps, bool pipelined) {
    if (!pipelined) {
        Elements row;
        row.push_back(text("  Current: ") | dim);
        row.push_back(make_stage_box(ps.stages[2], 2));
        row.push_back(text("  (single-cycle)") | dim | vcenter);
        return hbox(row);
    }
    Elements row;
    for (int i = 0; i < 5; ++i) {
        row.push_back(make_stage_box(ps.stages[i], i));
        if (i < 4) row.push_back(text(" → ") | dim | vcenter);
    }

    // Forwarding + hazard badges (Arches: flags as first-class UI)
    Elements fwd;
    fwd.push_back(text("  "));
    bool any = false;
    auto badge = [&](bool active, const char* lbl, Color col) {
        if (!active) return;
        any = true;
        fwd.push_back(text(std::string(" ") + lbl + " ") | color(col) | border);
        fwd.push_back(text(" "));
    };
    badge(ps.fwd_ex_to_ex_a,  "EX→EX rs",  Color::GreenLight);
    badge(ps.fwd_ex_to_ex_b,  "EX→EX rt",  Color::GreenLight);
    badge(ps.fwd_mem_to_ex_a, "WB→EX rs",  Color::CyanLight);
    badge(ps.fwd_mem_to_ex_b, "WB→EX rt",  Color::CyanLight);
    badge(ps.load_stall,      "LOAD-USE ⚠", Color::Yellow);
    badge(ps.branch_flush,    "BR FLUSH ✕", Color::Red);
    if (!any) fwd.push_back(text("no forwarding/hazards") | dim);

    return vbox({hbox(row), hbox(fwd) | size(HEIGHT, EQUAL, 1)});
}

// ─── Instruction decode panel ──────────────────────────────────────────────────
// Rows: field decode | asm reconstruction | binary breakdown (C) + labels
static Element render_instr_decode(const mips::IProcessor& cpu) {
    uint32_t pc = cpu.pc();
    auto fetched = cpu.mem().read_word(pc);
    if (!fetched)
        return hbox({text("  "), text("(no word at PC)") | dim});

    uint32_t raw = *fetched;
    auto dec_opt = mips::Decoder::decode(raw);
    if (!dec_opt) {
        return hbox({
            text("  "), text(std::format("0x{:08X}", raw)) | color(Color::Red) | bold,
            text("  ← unknown encoding") | dim,
        });
    }
    const auto& dec = *dec_opt;

    // ── Row 1: coloured field decode ─────────────────────────────────────────
    Elements f1;
    f1.push_back(text("  "));
    f1.push_back(text(std::string(mips::Decoder::mnemonic(dec))) | bold | color(Color::YellowLight));
    f1.push_back(text("   "));

    if (dec.format == mips::InstrFormat::R) {
        const auto& r = dec.r();
        f1.push_back(text("rd") | dim);
        f1.push_back(text(std::format(":${:<4}", kRegNames[r.rd])) | color(Color::GreenLight) | bold);
        f1.push_back(text("  rs") | dim);
        f1.push_back(text(std::format(":${:<4}", kRegNames[r.rs])) | color(Color::CyanLight) | bold);
        f1.push_back(text("  rt") | dim);
        f1.push_back(text(std::format(":${:<4}", kRegNames[r.rt])) | color(Color::MagentaLight) | bold);
        if (r.shamt) {
            f1.push_back(text("  sh") | dim);
            f1.push_back(text(std::format(":{}", r.shamt)) | color(Color::BlueLight));
        }
        f1.push_back(text(std::format("   [fn:0x{:02X}]",
            static_cast<unsigned>(r.funct))) | dim);
    } else if (dec.format == mips::InstrFormat::I) {
        const auto& i = dec.i();
        int32_t simm = mips::Decoder::sign_extend(i.imm);
        f1.push_back(text("rt") | dim);
        f1.push_back(text(std::format(":${:<4}", kRegNames[i.rt])) | color(Color::GreenLight) | bold);
        f1.push_back(text("  rs") | dim);
        f1.push_back(text(std::format(":${:<4}", kRegNames[i.rs])) | color(Color::CyanLight) | bold);
        f1.push_back(text("  imm") | dim);
        f1.push_back(text(std::format(":{:+d}", simm)) | color(Color::MagentaLight) | bold);
        f1.push_back(text(std::format(" [0x{:04X}]", i.imm)) | dim);
        const bool is_mem = (dec.opcode == mips::Opcode::LW  ||
                             dec.opcode == mips::Opcode::LBU ||
                             dec.opcode == mips::Opcode::LHU ||
                             dec.opcode == mips::Opcode::SW);
        if (is_mem)
            f1.push_back(text(std::format("  → ${}+{:+d}", kRegNames[i.rs], simm)) | dim);
    } else {
        uint32_t jaddr = ((pc + 4) & 0xF000'0000u) | (dec.j().target << 2);
        f1.push_back(text("target") | dim);
        f1.push_back(text(std::format(":0x{:07X}", dec.j().target)) | color(Color::CyanLight) | bold);
        f1.push_back(text(std::format("  → 0x{:08X}", jaddr)) | dim);
    }

    // ── Row 2: assembly reconstruction ────────────────────────────────────────
    Elements f2;
    f2.push_back(text("  asm: ") | dim);
    f2.push_back(text(reconstruct_asm(dec, pc)) | color(Color::White));

    // ── Rows 3+4: binary breakdown (C) ────────────────────────────────────────
    // Each field cell is (field_width + 1) chars wide so bits and labels align.
    Elements bits, lbls;
    bits.push_back(text("  bits: ") | dim);
    lbls.push_back(text("         ") | dim);

    auto add_field = [&](int hi, int lo, Color col, const char* lbl) {
        uint32_t mask = (hi - lo < 31) ? ((1u << (hi - lo + 1)) - 1u) : 0xFFFFFFFFu;
        uint32_t fv   = (raw >> lo) & mask;
        int      w    = hi - lo + 1;
        bits.push_back(text(std::format("{:0{}b}", fv, w)) | color(col) | bold
                       | size(WIDTH, EQUAL, w + 1));
        lbls.push_back(text(lbl) | color(col) | dim
                       | size(WIDTH, EQUAL, w + 1));
    };

    if (dec.format == mips::InstrFormat::R) {
        add_field(31, 26, Color::Yellow,        "op");
        add_field(25, 21, Color::CyanLight,     "rs");
        add_field(20, 16, Color::GreenLight,    "rt");
        add_field(15, 11, Color::MagentaLight,  "rd");
        add_field(10,  6, Color::BlueLight,     "sh");
        add_field( 5,  0, Color::RedLight,      "fn");
    } else if (dec.format == mips::InstrFormat::I) {
        add_field(31, 26, Color::Yellow,        "op");
        add_field(25, 21, Color::CyanLight,     "rs");
        add_field(20, 16, Color::GreenLight,    "rt");
        add_field(15,  0, Color::MagentaLight,  "imm");
    } else {
        add_field(31, 26, Color::Yellow,    "op");
        add_field(25,  0, Color::CyanLight, "target");
    }

    return vbox({hbox(f1), hbox(f2), hbox(bits), hbox(lbls)});
}

// ─── Execution trace panel ────────────────────────────────────────────────────
// Shows last N committed instructions; most recent at bottom with ▶ marker.
static Element render_exec_trace(const std::deque<TraceEntry>& trace) {
    if (trace.empty())
        return text("  (no instructions executed yet)") | dim;

    Elements rows;
    for (size_t i = 0; i < trace.size(); ++i) {
        const auto& e   = trace[i];
        bool is_last    = (i == trace.size() - 1);

        std::string asm_str = "???";
        if (auto d = mips::Decoder::decode(e.raw))
            asm_str = reconstruct_asm(*d, e.pc);

        Elements row;
        row.push_back(text(is_last ? " ▶ " : "   ") | color(Color::CyanLight));
        row.push_back(text(std::format("{:08X}", e.pc))
            | (is_last ? (bold | color(Color::Cyan)) : dim));
        row.push_back(text("  "));
        row.push_back(text(asm_str)
            | (is_last ? (bold | color(Color::White)) : dim));
        rows.push_back(hbox(row));
    }
    return vbox(rows);
}

// ─── Register file panel ──────────────────────────────────────────────────────
// B: non-zero registers show signed decimal in dim parentheses.
static Element render_registers(const mips::IProcessor& cpu) {
    Elements col_l, col_r;
    for (int i = 0; i < 32; ++i) {
        uint32_t  val     = cpu.regs().read(i);
        bool      changed = (i != 0 && i == cpu.regs().last_written());
        bool      nonzero = (val != 0);

        Color hex_col = Color::GrayDark;
        if      (changed) hex_col = Color::GreenLight;
        else if (nonzero) hex_col = Color::White;

        Elements row_els;
        row_els.push_back(text(std::format("${:<4}", kRegNames[i])) | dim);
        row_els.push_back(text(std::format(" {:08X}", val))
            | color(hex_col) | (changed ? bold : (nonzero ? bold : dim)));

        // B: signed decimal for non-zero values
        if (nonzero) {
            auto sv = static_cast<int32_t>(val);
            row_els.push_back(
                text(std::format(" ({:+d})", sv))
                | dim | color(changed ? Color::Green : Color::GrayDark));
        }

        (i < 16 ? col_l : col_r).push_back(hbox(row_els));
    }
    return window(
        text(" Registers "),
        hbox({vbox(col_l) | flex, separatorEmpty(), vbox(col_r) | flex})
    );
}

// ─── Memory hex-dump panel ────────────────────────────────────────────────────
// Shows addr | hex | mnemonic.  PC row is highlighted; halt instruction labelled.
// Binary breakdown moved to the decode panel (C).
static Element render_memory(const mips::IProcessor& cpu, uint32_t base, int rows) {
    uint32_t pc = cpu.pc();
    Elements lines;
    for (int r = 0; r < rows; ++r) {
        uint32_t addr = base + static_cast<uint32_t>(r * 4);
        auto word = cpu.mem().read_word(addr);
        if (!word) break;

        // Detect self-jump halt idiom
        bool is_halt = false;
        {
            auto raw_op = static_cast<mips::Opcode>((*word >> 26) & 0x3Fu);
            if (raw_op == mips::Opcode::J || raw_op == mips::Opcode::JAL) {
                uint32_t tgt   = *word & 0x03FF'FFFFu;
                uint32_t jaddr = ((addr + 4) & 0xF000'0000u) | (tgt << 2);
                if (jaddr == addr) is_halt = true;
            }
        }

        std::string mnem = "       ";
        if (auto d = mips::Decoder::decode(*word))
            mnem = std::format("{:<7}", std::string(mips::Decoder::mnemonic(*d)));

        bool is_pc = (addr == pc);

        Elements row_els;
        row_els.push_back(text(is_pc ? " ▶ " : "   ") | color(Color::CyanLight));
        row_els.push_back(text(std::format("{:08X}:", addr))
            | (is_pc ? (bold | color(Color::Cyan)) : dim));
        row_els.push_back(text(std::format(" {:08X}", *word))
            | (is_pc ? (bold | color(Color::YellowLight)) : color(Color::White)));
        row_els.push_back(text("  "));
        row_els.push_back(text(mnem)
            | (is_pc ? (bold | color(Color::YellowLight)) : dim));
        if (is_halt)
            row_els.push_back(text("halt") | color(Color::Red) | dim);

        lines.push_back(hbox(row_els));
    }
    if (lines.empty()) lines.push_back(text(" (empty)") | dim);
    return vbox(lines);
}

// ─── Ambient pipeline-flow strip ──────────────────────────────────────────────
// A single-row braille canvas: a bright "comet" sweeps left→right, looping, to
// suggest instruction flow through the pipeline. Purely decorative — it only
// animates while `active` is true (i.e. while the CPU is auto-running), so a
// paused dashboard stays calm. Canvas X/Y are braille sub-cells (2×4 per cell).
static Element render_flow_strip(std::size_t frame, bool active) {
    constexpr int W = 120, H = 4;   // 60 terminal cols × 1 row
    Canvas c = Canvas(W, H);
    // Baseline rail.
    for (int x = 0; x < W; ++x)
        c.DrawPoint(x, H / 2, true, Color::GrayDark);
    if (active) {
        const int head = static_cast<int>((frame * 3) % static_cast<std::size_t>(W));
        // A short comet: head bright, tail fading.
        for (int k = 0; k < 10; ++k) {
            const int x = (head - k + W) % W;
            const Color col = (k < 3) ? Color::CyanLight
                            : (k < 6) ? Color::Cyan
                                      : Color::Blue;
            c.DrawPoint(x, H / 2, true, col);
        }
    }
    return canvas(std::move(c)) | flex;
}

    // ─── Ambient oscilloscope panel ─────────────────────────────────────────────
    // Same visual technique as the startup splash (runSplash, below): a graticule
    // grid with three phase-shifted sine traces, drawn with DrawPointLine between
    // consecutive samples so the curves look continuous rather than dotted. Sized
    // for an in-app panel rather than fullscreen. Tied to live state in a small
    // way, mirroring render_flow_strip's "calm when idle" rule: phase advances
    // and amplitude swells while the CPU is auto-running, and settles to a slow
    // idle breathing pattern while paused/stepping, so it's lively without being
    // a distraction during step-by-step inspection.
    // Currently unreferenced: superseded as the ambient panel by the Core Pulse
    // animation, kept as an alternative visualisation.
    [[maybe_unused]] static Element render_oscilloscope_panel(std::size_t frame, bool active,
                                                              std::size_t cycle_count) {
        // D: the (width, height) passed to canvas(w, h, fn) are only a minimum-
        // size *requirement hint* for FTXUI's layout pass — at actual render
        // time CanvasNodeBase rebuilds the Canvas to match whatever box this
        // element ends up assigned (box_.x_max/y_max), then calls `fn` against
        // that. That's the difference from canvas(std::move(fixed_canvas)),
        // which bakes in a literal size up front and leaves everything past it
        // blank if the assigned box turns out bigger — which is exactly what
        // caused the dead space on the right/bottom previously.
        constexpr int kMinW = 60, kMinH = 16;

        return canvas(kMinW, kMinH, [frame, active, cycle_count](Canvas& c) {
            const int W   = c.width();
            const int H   = c.height();
            const int mid = H / 2;

            const float ph = static_cast<float>(frame) * (active ? 0.22f : 0.05f);

            // Graticule: dim grid + brighter center axis, same as the splash.
            for (int x = 0; x < W; x += 20)
                for (int y = 0; y < H; y += 4)
                    c.DrawPoint(x, y, true, Color::GrayDark);
            for (int y = 0; y < H; y += 16)
                for (int x = 0; x < W; x += 2)
                    c.DrawPoint(x, y, true, Color::GrayDark);
            for (int x = 0; x < W; ++x)
                c.DrawPoint(x, mid, true, Color::Blue);

            auto draw_wave = [&](float amp, float k, float phase, Color col) {
                int prev_x = 0;
                int prev_y = mid + static_cast<int>(amp * std::sin(k * 0.0f + phase));
                for (int x = 1; x < W; ++x) {
                    const float u = static_cast<float>(x);
                    const int   y = mid + static_cast<int>(amp * std::sin(k * u + phase));
                    c.DrawPointLine(prev_x, prev_y, x, y, col);
                    prev_x = x;
                    prev_y = y;
                }
            };

            // Idle: ~60% amplitude, slow breathing. Running: full swing, livelier.
            const float base_env = active ? 1.0f : 0.6f;
            const float env = base_env * (0.8f + 0.2f * std::sin(ph * 0.5f));

            // Scale amplitude to the granted height so the waves use the whole
            // panel on a large terminal instead of a fixed pixel swing that'd
            // look tiny once the dead-space bug above is fixed.
            const float amp_scale = static_cast<float>(H) / 48.0f;
            draw_wave(20.0f * env * amp_scale, 0.11f,  ph,            Color::CyanLight);
            draw_wave(14.0f * env * amp_scale, 0.16f,  ph * 1.3f + 1, Color::GreenLight);
            draw_wave( 8.0f * env * amp_scale, 0.23f, -ph * 1.7f + 2, Color::MagentaLight);

            // DrawText expects x even / y a multiple of 4 (one terminal cell).
            auto align_x = [](int v) { return std::max(0, (v / 2) * 2); };
            auto align_y = [](int v) { return std::max(0, (v / 4) * 4); };

            const int title_x = align_x(W / 2 - 18);
            c.DrawText(title_x, align_y(4), "CLEARCORE",
                       [](Cell& p) { p.foreground_color = Color::CyanLight; p.bold = true; });

            const int cycle_x = align_x(W / 2 - 26);
            c.DrawText(cycle_x, align_y(H - 8),
                       std::format("cycle {:<6}", cycle_count), Color::GrayLight);
        }) | flex;
    }

    // ─── Startup splash animation ─────────────────────────────────────────────────
    // An oscilloscope-style intro: layered sine waves sweep across a graticule grid
    // like a scope trace, phase-shifting over time so the waveform appears to travel.
    // The animation LOOPS continuously; press Enter (or Esc) to dismiss it and enter
    // the app.
    //
    // Implementation notes:
    //   • Canvas coordinates are in braille sub-cells: 2 px per cell on X, 4 on Y.
    //     A canvas of (W, H) sub-cells occupies (W/2, H/4) terminal cells.
    //   • Smooth curves come from DrawPointLine between adjacent samples, not lone
    //     points — that gives the continuous "trace" look of a real scope.
    //   • The ticker thread posts Event::Custom every frame; the renderer reads a
    //     frame counter and redraws. This mirrors the auto-run mechanism in runApp.
    static void runSplash() {
        using namespace std::chrono;

        auto screen = ScreenInteractive::Fullscreen();

        constexpr int kCanvasW = 200;   // braille sub-cells (→ 100 term cols)
        constexpr int kCanvasH = 80;    //                   (→  20 term rows)
        int           frame    = 0;
        std::atomic<bool> live{true};

        auto renderer = Renderer([&] {
            // Time in radians; advances each frame so the wave travels.
            const float ph = static_cast<float>(frame) * 0.18f;
            const int   mid = kCanvasH / 2;

            Canvas c = Canvas(kCanvasW, kCanvasH);

            // ── Graticule: dim grid lines like a scope screen ─────────────────
            // Vertical divisions every 20 sub-cells, horizontal every 16.
            for (int x = 0; x < kCanvasW; x += 20)
                for (int y = 0; y < kCanvasH; y += 4)
                    c.DrawPoint(x, y, true, Color::GrayDark);
            for (int y = 0; y < kCanvasH; y += 16)
                for (int x = 0; x < kCanvasW; x += 2)
                    c.DrawPoint(x, y, true, Color::GrayDark);
            // Brighter center axis (the scope's zero line).
            for (int x = 0; x < kCanvasW; ++x)
                c.DrawPoint(x, mid, true, Color::Blue);

            // ── A single sine trace, sampled and connected by short lines ──────
            // amp     : vertical swing in sub-cells
            // k       : spatial frequency (waves across the screen)
            // phase   : per-wave time offset so the layers travel independently
            // col     : trace colour
            auto draw_wave = [&](float amp, float k, float phase, Color col) {
                int prev_x = 0;
                int prev_y = mid + static_cast<int>(
                    amp * std::sin(k * 0.0f + phase));
                for (int x = 1; x < kCanvasW; ++x) {
                    const float u = static_cast<float>(x);
                    const int   y = mid + static_cast<int>(
                        amp * std::sin(k * u + phase));
                    c.DrawPointLine(prev_x, prev_y, x, y, col);
                    prev_x = x;
                    prev_y = y;
                }
            };

            // Amplitude "breathes" gently via a slow envelope.
            const float env = 0.80f + 0.20f * std::sin(ph * 0.5f);

            // Three layered traces, phase-shifted, evoking a multi-channel scope.
            draw_wave(26.0f * env, 0.11f,  ph,            Color::CyanLight);
            draw_wave(18.0f * env, 0.16f,  ph * 1.3f + 1, Color::GreenLight);
            draw_wave(11.0f * env, 0.23f, -ph * 1.7f + 2, Color::MagentaLight);

            // ── Title overlaid on the trace ───────────────────────────────────
            // DrawText y should be a multiple of 4 (normal-character rows).
            c.DrawText(kCanvasW / 2 - 26, mid - 24, "C L E A R C O R E",
                       [](Cell& p) { p.foreground_color = Color::CyanLight;
                                     p.bold = true; });
            c.DrawText(kCanvasW / 2 - 18, mid + 20, "MIPS  EMULATOR",
                       Color::GrayLight);

            return vbox({
                filler(),
                hbox({ filler(), canvas(std::move(c)) | border, filler() }),
                filler(),
                text("press [Enter] to start") | bold | center
                    | color(Color::CyanLight),
                text("oscilloscope warming up…") | dim | center,
            }) | flex;
        });

        // Enter or Esc dismisses the splash; everything else is ignored so the
        // wave keeps running until the user is ready.
        auto root = CatchEvent(renderer, [&](const Event& e) {
            if (e == Event::Custom) return true;
            if (e == Event::Return || e == Event::Escape) {
                live.store(false);
                screen.Exit();
                return true;
            }
            return false;
        });

        // Continuous ~30 fps loop — runs until the user presses Enter/Esc.
        std::thread ticker([&] {
            while (live.load()) {
                std::this_thread::sleep_for(milliseconds(33));
                ++frame;
                screen.PostEvent(Event::Custom);
            }
        });

        screen.Loop(root);
        live.store(false);
        ticker.join();
    }

    // ─── Reactive flow-field background for "Core Pulse" ─────────────────────────
    // A layered wireframe surface that fills the whole panel viewport, animates
    // continuously on its own (independent of CPU cycle stepping), and is gently
    // attracted toward the mouse — but only while the cursor is inside this
    // panel. `mouse_active` is computed once in the event handler against an
    // approximate panel bounding box (see runApp()'s CatchEvent).
    static Component create_datapath_3d_background(
        int& mouse_x,
        int& mouse_y,
        bool& mouse_active,
        uint64_t cycle_counter) {

      return Renderer([&, cycle_counter] {
        // Persistent across frames: this lambda's compiled body is shared by
        // every Renderer instance create_datapath_3d_background() produces,
        // so these statics behave like continuous animation state rather
        // than resetting each time the tab redraws.
        static float anim_time   = 0.0f;
        static float idle_phase  = 0.0f;
        static float smoothed_mx = 0.0f;
        static float smoothed_my = 0.0f;

        anim_time  += 0.035f;  // smooth motion, independent of CPU stepping
        idle_phase += 0.012f;

        // ── Canvas fills the actual terminal viewport ───────────────────────
        // Braille sub-cells: ÷2 per column, ÷4 per row.
        const auto term       = Terminal::Size();
        const int  avail_cols = std::max(60, term.dimx - 6);
        const int  panel_rows = std::clamp(term.dimy / 3, 14, 26);
        const int  kCanvasW   = avail_cols * 2;
        const int  kCanvasH   = panel_rows  * 4;

        Canvas c(kCanvasW, kCanvasH);

        // ── Attractor target ─────────────────────────────────────────────────
        // Mouse position when inside the panel; otherwise a slow, independent
        // Lissajous wander so the field keeps moving instead of freezing.
        float target_x, target_y;
        if (mouse_active) {
          target_x = (static_cast<float>(mouse_x) /
                      static_cast<float>(std::max(1, term.dimx))) * kCanvasW;
          target_y = (static_cast<float>(mouse_y) /
                      static_cast<float>(panel_rows)) * kCanvasH;
        } else {
          target_x = kCanvasW * 0.5f + std::sin(idle_phase * 0.7f) * (kCanvasW * 0.30f);
          target_y = kCanvasH * 0.5f + std::cos(idle_phase * 0.9f) * (kCanvasH * 0.30f);
        }

        // Exponential smoothing — the attractor glides, it never snaps.
        constexpr float kSmooth = 0.08f;
        smoothed_mx += (target_x - smoothed_mx) * kSmooth;
        smoothed_my += (target_y - smoothed_my) * kSmooth;

        // ── Field height ──────────────────────────────────────────────────────
        // Three traveling waves at golden-ratio-spaced frequencies (no visible
        // repeat period) plus a breathing gaussian at the attractor and an
        // outward ripple. Driven by anim_time so motion stays smooth even when
        // the CPU is paused; cycle_counter only nudges phase so stepping still
        // visibly perturbs the field without owning the animation clock.
        constexpr float kPhi = 1.61803398875f;
        auto surface_height = [&](float x, float y) -> float {
          const float dx = x - smoothed_mx;
          const float dy = y - smoothed_my;
          const float dist_sq = dx * dx + dy * dy;
          const float dist    = std::sqrt(dist_sq);

          const float breathe  = 0.85f + 0.15f * std::sin(anim_time * 0.6f);
          const float gaussian = std::exp(-0.0018f * dist_sq) * breathe;

          const float w1 = std::sin(x * 0.020f + anim_time * 1.00f);
          const float w2 = std::sin(y * 0.020f * kPhi - anim_time * 0.62f);
          const float w3 = std::sin((x + y) * 0.012f + anim_time * 0.38f +
                                     static_cast<float>(cycle_counter) * 0.05f);
          const float field = (w1 + w2 + w3) * 4.0f;

          const float ripple = 5.0f * std::sin(dist * 0.05f - anim_time * 1.4f) *
                                std::exp(-dist * 0.004f);

          return field + ripple + gaussian * 55.0f;
        };

        // ── Mesh ──────────────────────────────────────────────────────────────
        constexpr int GRID_SIZE = 18;
        std::vector<std::vector<float>> heights(
            GRID_SIZE, std::vector<float>(GRID_SIZE));

        for (int gy = 0; gy < GRID_SIZE; ++gy) {
          for (int gx = 0; gx < GRID_SIZE; ++gx) {
            const float fx = (static_cast<float>(gx) / (GRID_SIZE - 1)) * kCanvasW;
            const float fy = (static_cast<float>(gy) / (GRID_SIZE - 1)) * kCanvasH;
            heights[gy][gx] = surface_height(fx, fy);
          }
        }

        auto project = [&](int gx, int gy, float h) -> std::pair<int, int> {
          const float base_x = (static_cast<float>(gx) / (GRID_SIZE - 1)) * kCanvasW;
          const float base_y = (static_cast<float>(gy) / (GRID_SIZE - 1)) * kCanvasH;
          const int px = static_cast<int>(base_x + 0.55f * h);
          const int py = static_cast<int>(kCanvasH - base_y - 0.85f * h);
          return {px, py};
        };

        // Smooth teal → gold → crimson gradient (continuous RGB, not bands).
        auto height_color = [](float h) -> Color {
          const float t = std::clamp((h + 10.0f) / 70.0f, 0.0f, 1.0f);
          if (t < 0.5f) {
            const float u = t / 0.5f;
            return Color::RGB(
                static_cast<uint8_t>(48  + u * (212 - 48)),
                static_cast<uint8_t>(214 - u * (214 - 175)),
                static_cast<uint8_t>(186 - u * (186 - 55)));
          }
          const float u = (t - 0.5f) / 0.5f;
          return Color::RGB(
              static_cast<uint8_t>(212 + u * (220 - 212)),
              static_cast<uint8_t>(175 - u * (175 - 45)),
              static_cast<uint8_t>(55  - u * (55  - 60)));
        };

        for (int gy = 0; gy < GRID_SIZE; ++gy) {
          for (int gx = 1; gx < GRID_SIZE; ++gx) {
            const float h1 = heights[gy][gx - 1];
            const float h2 = heights[gy][gx];
            const auto [x1, y1] = project(gx - 1, gy, h1);
            const auto [x2, y2] = project(gx,     gy, h2);
            c.DrawPointLine(x1, y1, x2, y2, height_color(h2));
          }
        }
        for (int gx = 0; gx < GRID_SIZE; ++gx) {
          for (int gy = 1; gy < GRID_SIZE; ++gy) {
            const float h1 = heights[gy - 1][gx];
            const float h2 = heights[gy][gx];
            const auto [x1, y1] = project(gx, gy - 1, h1);
            const auto [x2, y2] = project(gx, gy,     h2);
            c.DrawPointLine(x1, y1, x2, y2, height_color(h2));
          }
        }

        c.DrawText(4, 0, "Core Pulse", [](Cell& p) {
          p.foreground_color = Color::Green;
          p.bold = true;
        });

        c.DrawText(kCanvasW - 70, kCanvasH - 8,
                   "cycle " + std::to_string(cycle_counter), [](Cell& p) {
          p.foreground_color = Color::White;
          p.underlined = true;
        });

        return canvas(std::move(c));
      });
    }

    // ─── runApp ───────────────────────────────────────────────────────────────────
    int runApp() {

        // run the splash screen()
        runSplash();
        // ── Mouse tracking for the Core Pulse panel ─────────────────────────────────
        // global_mouse_active is only true while the cursor is inside the panel's
        // approximate bounding box (computed in the event handler below) AND that
        // tab is active — see kCorePulseTopRow / panel_rows there.
        int  global_mouse_x      = 0;
        int  global_mouse_y      = 0;
        bool global_mouse_active = false;
        // ── Core state ────────────────────────────────────────────────────────────
        Converter converter;
        CpuMode cpu_mode = CpuMode::SingleCycle;
        std::unique_ptr<mips::IProcessor> cpu = std::make_unique<mips::SingleCycleCpu>();

        // Auto-run: background thread posts Event::Custom; step runs on UI thread
        std::atomic<bool> auto_run{false};
        std::atomic<bool> app_live{true};
        int speed_ms = 200;  // plain int for Slider (int*)

        // Ambient animation tick — advanced by the ticker every frame, read by
        // the status-bar spinner and the dashboard flow strip. Declared before
        // the ticker lambda so its capture-by-reference is valid.
        std::size_t anim_frame = 0;

        // Telemetry
        std::size_t tel_cycles = 0, tel_stalls = 0, tel_forwards = 0, tel_flushes = 0;

        // Execution trace (last 8 committed instructions)
        std::deque<TraceEntry> exec_trace;

        // Memory view
        uint32_t mem_base = 0;

        // Loader
        bool loader_ok = false;
        std::string loader_status = "Ready.";

        // Converter
        bool syncing = false;
        std::string dec_str = "0", hex_str = "0", bin_str = "0";
        std::string nibble_str = "0000", mnem_str = "SLL";

        // Config
        std::vector<std::string> cpu_mode_names = {"Single-Cycle", "Pipelined (5-stage)"};
        int cpu_mode_idx = 0;

        // Tabs
        // D: plain glyphs only — FTXUI's IsFullWidth() table has no entries in
        // the emoji block (0x1F3xx-0x1F9xx), so codepoint_width() reports 1 for
        // emoji while terminals render them at 2 cells. That mismatch desyncs
        // size(WIDTH, GREATER_THAN, ...) and right-edge alignment per glyph used.
        int tab_idx = 0;
        std::vector<std::string> tab_labels = {
            " Converter ", " CPU Dashboard ", " CPU Config ", " Program Loader ",
            " Core Pulse ", " Utility Tools ",
        };

        // ── Helpers ───────────────────────────────────────────────────────────────
        auto reset_tel = [&] {
            tel_cycles = tel_stalls = tel_forwards = tel_flushes = 0;
            exec_trace.clear();
        };

        // do_step — UI thread only.  Updates telemetry and execution trace.
        auto do_step = [&] {
            auto result = cpu->step();
            ++tel_cycles;
            const auto& ps = cpu->pipeline_state();

            // Execution trace: WB stage for pipelined, EX for single-cycle
            bool is_pl = (cpu_mode == CpuMode::Pipelined);
            const auto& ts = ps.stages[is_pl ? 4 : 2];
            if (ts.valid && !ts.stalled) {
                uint32_t tpc = ts.pc;
                // WB stage raw is 0; look up from memory (instruction still there)
                if (auto w = cpu->mem().read_word(tpc)) {
                    exec_trace.push_back({tpc, *w});
                    while (exec_trace.size() > 8) exec_trace.pop_front();
                }
            }

            if (ps.load_stall)   ++tel_stalls;
            if (ps.branch_flush) ++tel_flushes;
            if (ps.fwd_ex_to_ex_a || ps.fwd_ex_to_ex_b ||
                ps.fwd_mem_to_ex_a || ps.fwd_mem_to_ex_b) ++tel_forwards;
            if (result != mips::StepResult::Ok) auto_run.store(false);
            return result;
        };

        // ── Screen + background ticker ────────────────────────────────────────────
        auto screen = ScreenInteractive::Fullscreen();

        std::thread ticker([&] {
            int idle_tick = 0;
            while (app_live.load()) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(std::max(10, speed_ms)));
                ++anim_frame;
                if (auto_run.load()) {
                    screen.PostEvent(Event::Custom);
                } else if (++idle_tick % 6 == 0) {
                    // A gentle ~every-1.2s repaint so the ambient spinner breathes
                    // without burning CPU while the machine is paused.
                    screen.PostEvent(Event::Custom);
                }
            }
        });

        // ── Converter components ──────────────────────────────────────────────────
        auto update_views = [&](Converter::Base base, const std::string& s) {
            if (syncing) return;
            syncing = true;
            if (converter.update(s, base)) {
                if (base != Converter::Base::Decimal) dec_str = converter.as(Converter::Base::Decimal);
                if (base != Converter::Base::Hex)     hex_str = converter.as(Converter::Base::Hex);
                if (base != Converter::Base::Binary)  bin_str = converter.as(Converter::Base::Binary);
                nibble_str = converter.bits();
                uint64_t v = converter.value();
                mnem_str   = (v <= 0xFFFF'FFFFu)
                    ? (mips::Decoder::decode(static_cast<uint32_t>(v))
                        ? std::string(mips::Decoder::mnemonic(*mips::Decoder::decode(static_cast<uint32_t>(v))))
                        : "Invalid")
                    : "(out of 32-bit range)";
            }
            syncing = false;
        };

        InputOption dec_opt; dec_opt.on_change = [&]{ update_views(Converter::Base::Decimal, dec_str); };
        InputOption hex_opt; hex_opt.on_change = [&]{ update_views(Converter::Base::Hex,     hex_str); };
        InputOption bin_opt; bin_opt.on_change = [&]{ update_views(Converter::Base::Binary,  bin_str); };

        Component dec_input = Input(&dec_str, "0", dec_opt);
        Component hex_input = Input(&hex_str, "0", hex_opt);
        Component bin_input = Input(&bin_str, "0", bin_opt);
        Component conv_container = Container::Vertical({dec_input, hex_input, bin_input});

        // ── CPU control components ────────────────────────────────────────────────
        Component btn_step = Button(" Step ", [&] { do_step(); });

        ButtonOption auto_opt;
        auto_opt.transform = [&](const EntryState& s) {
            bool run = auto_run.load();
            Element e = text(run ? " ■ Stop " : " ▶ Auto ")
                      | color(run ? Color::Red : Color::GreenLight);
            if (s.focused) e = e | inverted;
            return e | border;
        };
        Component btn_auto = Button("", [&] { auto_run.store(!auto_run.load()); }, auto_opt);

        Component btn_run = Button(" Run→Halt ", [&] {
            auto_run.store(false);
            cpu->run(100'000);
            tel_cycles = cpu->cycle_count();
            exec_trace.clear();
        });

        Component btn_reset = Button(" Reset ", [&] {
            auto_run.store(false);
            cpu->reset(false);
            reset_tel();
            mem_base = 0;
            loader_status = "CPU reset.";
        });

        Component speed_slider = Slider("", &speed_ms, 10, 1000, 10);

        Component ctrl_container = Container::Horizontal({
            btn_step, btn_auto, btn_run, btn_reset, speed_slider
        });

        // ── Config components ─────────────────────────────────────────────────────
        RadioboxOption rb_opt;
        rb_opt.transform = [](const EntryState& s) {
            Element e = hbox({
                text(s.active ? " ◉  " : " ○  ")
                    | color(s.active ? Color::Cyan : Color::GrayDark),
                text(s.label) | (s.active ? (bold | color(Color::White)) : dim),
            });
            if (s.focused) e = e | inverted;
            return e;
        };
        Component cpu_selector = Radiobox(&cpu_mode_names, &cpu_mode_idx, rb_opt);
        Component btn_apply = Button(" Apply ", [&] {
            auto_run.store(false);
            cpu->reset(true);
            reset_tel();
            mem_base = 0;
            if (cpu_mode_idx == 0) {
                cpu = std::make_unique<mips::SingleCycleCpu>();
                cpu_mode = CpuMode::SingleCycle;
                loader_status = "Switched to Single-Cycle.";
            } else {
                cpu = std::make_unique<mips::PipelinedCpu>();
                cpu_mode = CpuMode::Pipelined;
                loader_status = "Switched to Pipelined (5-stage).";
            }
        });
        Component cfg_container = Container::Vertical({cpu_selector, btn_apply});

        // ── Loader components ─────────────────────────────────────────────────────
        std::string filepath = "program.hex";
        Component filepath_input = Input(&filepath, "path/to/program.hex");
        Component btn_load = Button(" Load ", [&] {
            auto_run.store(false);
            cpu->reset(true);
            reset_tel();
            mem_base = 0;
            loader_ok = load_hex_file(filepath, *cpu, loader_status);
        });
        Component loader_container = Container::Vertical({filepath_input, btn_load});

        // ── Tab navigation ────────────────────────────────────────────────────────
        MenuOption tab_opt = MenuOption::HorizontalAnimated();
        tab_opt.entries_option.transform = [](const EntryState& s) {
            Element e = text(s.label) | center;
            if (s.active)  e = e | bold | color(Color::Cyan);
            if (s.focused) e = e | inverted;
            return e | size(WIDTH, GREATER_THAN, 16);
        };
        Component tab_menu = Menu(&tab_labels, &tab_idx, tab_opt);

        // D: tabs 4 (Core Pulse) and 5 (Utility Tools) render purely from cpu
        // state with no Input/Button/Slider of their own — these empty containers
        // exist only to keep Container::Tab's child count equal to tab_labels.size()
        // so index 4/5 don't alias onto an earlier tab's live components.
        Component dp_focus   = Container::Vertical({});
        Component util_focus = Container::Vertical({});

        Component main_container = Container::Vertical({
            tab_menu,
            // D: Container::Tab selects children()[*selector % children().size()].
            // tab_labels has 6 entries (tab_idx 0-5) but this list previously had
            // only 4 — on tab 4 that wrapped to index 0 (Converter) and on tab 5
            // to index 1 (CPU controls), silently routing keyboard/mouse focus to
            // off-screen components (e.g. Enter on tab 5 could fire Run/Reset).
            // dp_focus/util_focus are empty containers — ComponentBase::Focusable()
            // returns false with no children, so they correctly absorb focus
            // without doing anything, matching those tabs' non-interactive content.
            Container::Tab({
                conv_container, ctrl_container, cfg_container, loader_container,
                dp_focus, util_focus,
            }, &tab_idx),
        });

        // ── Renderer ──────────────────────────────────────────────────────────────
        Component renderer = Renderer(main_container, [&] {
            Element content;

            // ══ TAB 0: Converter ══════════════════════════════════════════════════
            if (tab_idx == 0) {
                auto v32 = static_cast<uint32_t>(converter.value() & 0xFFFF'FFFFu);

                // ── R-format bit breakdown (preserved, moved to bottom full-width) ──
                auto field_box = [&](int hi, int lo, Color col, const char* rng, const char* lbl) {
                    uint32_t mask = (hi < 31 || lo > 0) ?
                        ((1u << (hi-lo+1))-1u) : 0xFFFFFFFFu;
                    uint32_t fv   = (v32 >> lo) & mask;
                    int      w    = hi - lo + 1;
                    return window(
                        text(rng) | dim | color(col),
                        vbox({
                            text(std::format("{:0{}b}", fv, w)) | center | color(col) | bold,
                            text(lbl) | center | dim,
                        })
                    );
                };
                Elements bits_row;
                bits_row.push_back(field_box(31,26,Color::Yellow,      "[31:26]","opcode"));
                bits_row.push_back(field_box(25,21,Color::CyanLight,   "[25:21]","rs"));
                bits_row.push_back(field_box(20,16,Color::GreenLight,  "[20:16]","rt"));
                bits_row.push_back(field_box(15,11,Color::MagentaLight,"[15:11]","rd/hi"));
                bits_row.push_back(field_box(10, 6,Color::BlueLight,   "[10:6]", "shamt"));
                bits_row.push_back(field_box( 5, 0,Color::RedLight,    "[ 5:0]", "funct"));

                // ── Byte-grouped nibble display: MSB·byte·byte·byte·LSB ──────────
                Elements nibble_els;
                nibble_els.push_back(text(std::format("{:02X}", (v32 >> 24) & 0xFF)) | color(Color::Cyan) | bold);
                nibble_els.push_back(text("  ·  ") | dim);
                nibble_els.push_back(text(std::format("{:02X}", (v32 >> 16) & 0xFF)) | color(Color::Cyan) | bold);
                nibble_els.push_back(text("  ·  ") | dim);
                nibble_els.push_back(text(std::format("{:02X}", (v32 >>  8) & 0xFF)) | color(Color::Cyan) | bold);
                nibble_els.push_back(text("  ·  ") | dim);
                nibble_els.push_back(text(std::format("{:02X}",  v32        & 0xFF)) | color(Color::Cyan) | bold);

                // ── Decode the current converter value ───────────────────────────
                auto decode_opt = mips::Decoder::decode(v32);

                // Full assembly string — reuses reconstruct_asm already in this file.
                // PC is 0: fine for R/I format; J-format address will be relative to 0.
                const std::string asm_line = decode_opt
                    ? reconstruct_asm(*decode_opt, 0u)
                    : "(invalid instruction)";

                // ── Field values — bit-extracted directly to stay decoupled from  ──
                // InstrInfo's internal variant layout.
                const uint32_t f_rs    = (v32 >> 21) & 0x1F;
                const uint32_t f_rt    = (v32 >> 16) & 0x1F;
                const uint32_t f_rd    = (v32 >> 11) & 0x1F;
                const uint32_t f_shamt = (v32 >>  6) & 0x1F;
                const uint32_t f_funct =  v32        & 0x3F;

                Elements field_els;
                if (!decode_opt) {
                    field_els.push_back(text("  (not a valid MIPS instruction)") | dim);
                } else if (decode_opt->format == mips::InstrFormat::R) {
                    field_els.push_back(hbox({ text("  rs  ") | dim, text(std::format("${}", kRegNames[f_rs]))   | color(Color::CyanLight)    }));
                    field_els.push_back(hbox({ text("  rt  ") | dim, text(std::format("${}", kRegNames[f_rt]))   | color(Color::GreenLight)   }));
                    field_els.push_back(hbox({ text("  rd  ") | dim, text(std::format("${}", kRegNames[f_rd]))   | color(Color::MagentaLight) }));
                    field_els.push_back(hbox({ text("  sa  ") | dim, text(std::format("{}", f_shamt))            | color(Color::BlueLight)    }));
                    field_els.push_back(hbox({ text("  fn  ") | dim, text(std::format("0x{:02X}", f_funct))      | color(Color::RedLight)     }));
                } else if (decode_opt->format == mips::InstrFormat::I) {
                    const auto&   iv   = decode_opt->i();
                    const int32_t simm = mips::Decoder::sign_extend(iv.imm);
                    field_els.push_back(hbox({ text("  rs  ") | dim, text(std::format("${}", kRegNames[f_rs]))         | color(Color::CyanLight)  }));
                    field_els.push_back(hbox({ text("  rt  ") | dim, text(std::format("${}", kRegNames[f_rt]))         | color(Color::GreenLight) }));
                    field_els.push_back(hbox({ text("  imm ") | dim, text(std::format("0x{:04X}  ({})", iv.imm, simm)) | color(Color::BlueLight)  }));
                } else { // J-format
                    const auto&    jv    = decode_opt->j();
                    const uint32_t jaddr = (4u & 0xF000'0000u) | (jv.target << 2);
                    field_els.push_back(hbox({ text("  tgt ") | dim, text(std::format("0x{:07X}", jv.target)) | color(Color::CyanLight) }));
                    field_els.push_back(hbox({ text("  →   ") | dim, text(std::format("0x{:08X}", jaddr))    | dim                     }));
                }

                // ── Control signals — 2-column grid using derive_control ─────────
                // derive_control() is a free function in the mips:: namespace
                // (processor.h / single_cycle_cpu.cpp). It is side-effect-free.
                Elements ctrl_col_a, ctrl_col_b;
                if (decode_opt) {
                    const mips::Control ctrl = mips::derive_control(*decode_opt);
                    // No outer-scope captures needed: all data arrives via parameters.
                    auto sig = [](Elements& col, const char* name, bool val) {
                        Elements row;
                        row.push_back(text(std::format("  {:<7} ", name)) | dim);
                        row.push_back(
                            text(val ? "1" : "0")
                            | color(val ? Color::GreenLight : Color::GrayDark) | bold
                        );
                        col.push_back(hbox(row));
                    };
                    sig(ctrl_col_a, "RegWr",  ctrl.reg_write);
                    sig(ctrl_col_a, "MemRd",  ctrl.mem_read);
                    sig(ctrl_col_a, "MemWr",  ctrl.mem_write);
                    sig(ctrl_col_a, "MtoReg", ctrl.mem_to_reg);
                    sig(ctrl_col_b, "ALUSrc", ctrl.alu_src);
                    sig(ctrl_col_b, "RegDst", ctrl.reg_dst);
                    sig(ctrl_col_b, "Branch", ctrl.branch);
                    sig(ctrl_col_b, "Jump",   ctrl.jump);
                } else {
                    ctrl_col_a.push_back(text("  n/a") | dim);
                }

                // ── Two-column split layout ──────────────────────────────────────
                // Left: inputs (all editable) + nibble view
                // window() puts the label inside the border line — FTXUI dom ref §window
                auto left_col = vbox({
                    window(text(" DEC ") | dim,
                           hbox({ text(" "), dec_input->Render() | flex })),
                    window(text(" HEX ") | color(Color::Cyan),
                           hbox({ text(" "), hex_input->Render() | flex })),
                    window(text(" BIN ") | dim,
                           hbox({ text(" "), bin_input->Render() | flex })),
                    separatorEmpty(),
                    window(text(" Nibble  MSB ── byte ── byte ── byte ── LSB "),
                           hbox(nibble_els) | center),
                }) | flex;

                // Right: live decode result, field breakdown, control signals
                auto right_col = vbox({
                    window(text(" Live MIPS decode "),
                           vbox({
                               hbox({ text("  "), text(mnem_str) | color(Color::YellowLight) | bold }),
                               hbox({ text("  "), text(asm_line) | color(Color::White) | dim }),
                           })),
                    window(text(" Field values "), vbox(field_els)),
                    window(text(" Control signals "),
                           hbox({ vbox(ctrl_col_a) | flex, vbox(ctrl_col_b) | flex })),
                }) | flex;

                content = vbox({
                    hbox({ left_col, separator(), right_col }),
                    separatorEmpty(),
                    window(text(" R-format bit breakdown (32-bit) "), hbox(bits_row)),
                });
            }

            // ══ TAB 1: CPU Dashboard ══════════════════════════════════════════════
            else if (tab_idx == 1) {
                const auto& ps = cpu->pipeline_state();

                // ── PC / mode header bar ─────────────────────────────────────────
                Element header_bar = hbox({
                    text(" PC: ") | dim,
                    text(std::format("0x{:08X}", cpu->pc())) | bold | color(Color::Cyan),
                    text("   Cycle: ") | dim,
                    text(std::format("{}", cpu->cycle_count())) | bold | color(Color::YellowLight),
                    text("   "),
                    text(cpu_mode == CpuMode::SingleCycle
                         ? "Single-Cycle" : "5-stage Pipeline") | dim,
                    filler(),
                });

                // ── Pipeline visualization ─────────────────────────────────────
                Element flow = render_flow_strip(anim_frame, auto_run.load());
                Element pipeline_panel = window(
                    text(cpu_mode == CpuMode::Pipelined
                         ? " Pipeline  IF → ID → EX → MEM → WB "
                         : " Execution State "),
                    vbox({header_bar, flow, separatorEmpty(),
                          render_pipeline(ps, cpu_mode == CpuMode::Pipelined)})
                );

                // ── Instruction decode (fields + asm + binary breakdown) ─────────
                Element decode_panel = window(
                    text(" Instruction Decode "),
                    render_instr_decode(*cpu)
                );

                // ── Execution trace ──────────────────────────────────────────────
                Element trace_panel = window(
                    text(" Execution Trace  (last 8 committed) "),
                    render_exec_trace(exec_trace)
                );

                // ── Memory hex-dump ──────────────────────────────────────────────
                Element mem_panel = window(
                    text(std::format(" Memory @{:08X}  PgUp/Dn · Home=PC ", mem_base)),
                    render_memory(*cpu, mem_base, 14)
                ) | size(WIDTH, EQUAL, 36);

                // ── Telemetry with gauge bars ────────────────────────────────────
                float stall_pct = tel_cycles > 0
                    ? static_cast<float>(tel_stalls) / static_cast<float>(tel_cycles) : 0.0f;
                float fwd_pct   = tel_cycles > 0
                    ? static_cast<float>(tel_forwards) / static_cast<float>(tel_cycles) : 0.0f;
                float flush_pct = tel_cycles > 0
                    ? static_cast<float>(tel_flushes) / static_cast<float>(tel_cycles) : 0.0f;
                double cpi      = (tel_cycles > 0 && tel_cycles > tel_stalls)
                    ? static_cast<double>(tel_cycles) / static_cast<double>(tel_cycles - tel_stalls)
                    : 1.0;

                auto tel_cell = [](const char* lbl, std::size_t n, float pct, Color col) {
                    Elements e;
                    e.push_back(text(lbl) | dim);
                    e.push_back(text(std::format("{}", n)) | bold | color(col));
                    e.push_back(text(std::format("  ({:.0f}%)", pct * 100.0f)) | dim);
                    e.push_back(gauge(pct) | color(col) | size(HEIGHT, EQUAL, 1));
                    return vbox(e) | flex;
                };

                Element tel_panel = window(text(" Telemetry "), hbox({
                    tel_cell("cycles    ", tel_cycles, 0.0f, Color::White),
                    separatorEmpty(),
                    tel_cell("stalls    ", tel_stalls,   stall_pct, Color::Yellow),
                    separatorEmpty(),
                    tel_cell("forwards  ", tel_forwards, fwd_pct,   Color::GreenLight),
                    separatorEmpty(),
                    tel_cell("flushes   ", tel_flushes,  flush_pct, Color::Red),
                    separatorEmpty(),
                    vbox({
                        text("CPI") | dim,
                        text(std::format("{:.2f}", cpi)) | bold | color(Color::CyanLight),
                    }) | flex,
                }));

                // ── Controls ─────────────────────────────────────────────────────
                Element ctrl_panel = window(text(" Controls "), hbox({
                    btn_step->Render(),
                    text(" "),
                    btn_auto->Render(),
                    text(" "),
                    btn_run->Render(),
                    text(" "),
                    btn_reset->Render(),
                    text("  Speed: ") | dim | vcenter,
                    speed_slider->Render() | size(WIDTH, EQUAL, 16) | vcenter,
                    text(std::format(" {}ms ", speed_ms)) | dim | vcenter,
                }));

                // ── Layout ───────────────────────────────────────────────────────
                content = vbox({
                    hbox({
                        render_registers(*cpu) | size(WIDTH, EQUAL, 40),
                        separatorEmpty(),
                        vbox({pipeline_panel, decode_panel, trace_panel}) | flex,
                        separatorEmpty(),
                        mem_panel,
                    }) | flex,
                    tel_panel,
                    ctrl_panel,
                });
            }

            // ══ TAB 2: CPU Config ═════════════════════════════════════════════════
            else if (tab_idx == 2) {
                // Pending change: selected != currently running
                const bool pending = (cpu_mode_idx == 0)
                    ? (cpu_mode != CpuMode::SingleCycle)
                    : (cpu_mode != CpuMode::Pipelined);

                // ── Left: selection + active badge + apply ────────────────────────
                Element active_badge =
                    text(cpu_mode == CpuMode::SingleCycle ? "  Single-Cycle " : "  Pipelined ")
                    | bold | color(Color::Black)
                    | bgcolor(cpu_mode == CpuMode::SingleCycle
                              ? Color::GreenLight : Color::YellowLight);

                Element apply_row = hbox({
                    text("  "),
                    btn_apply->Render(),
                    text("  ") ,
                    pending
                        ? (text("← pending change") | color(Color::Yellow))
                        : (text("✓ already active") | color(Color::GreenLight) | dim),
                });

                auto sel_panel = window(text(" CPU Implementation "),
                    vbox({
                        hbox({ text("  Active: ") | dim, active_badge }),
                        separatorEmpty(),
                        separatorStyled(LIGHT),
                        separatorEmpty(),
                        cpu_selector->Render(),
                        separatorEmpty(),
                        separatorStyled(LIGHT),
                        separatorEmpty(),
                        apply_row,
                    })
                ) | size(WIDTH, EQUAL, 40);

                // ── Right: description of the selected mode ───────────────────────
                const bool show_pl = (cpu_mode_idx == 1);

                // paragraph() wraps long lines automatically — FTXUI dom §paragraph
                const char* desc_title = show_pl
                    ? " Pipelined CPU (5-stage) "
                    : " Single-Cycle CPU ";
                const char* desc_body = show_pl
                    ? "Overlaps up to five instructions simultaneously. "
                      "Each stage (IF, ID, EX, MEM, WB) holds a different "
                      "instruction each cycle, so throughput approaches one "
                      "instruction per cycle in steady state. "
                      "Data hazards are resolved by forwarding (EX/MEM→EX, "
                      "MEM/WB→EX) and load-use stalls. "
                      "Control hazards (branch, jump) cause 1-2 cycle flushes."
                    : "Executes one instruction per clock cycle. "
                      "All stages — fetch, decode, execute, memory, writeback — "
                      "complete in a single tick before the next instruction begins. "
                      "CPI is exactly 1.0; no hazards; easy to reason about. "
                      "Good starting point before enabling the pipeline.";
                const char* desc_ref = show_pl
                    ? "  H&H §8 / Patterson & Hennessy §4.5–4.9"
                    : "  H&H §7 / Patterson & Hennessy §4.1–4.4";

                // Telemetry summary (if any cycles have run)
                Elements stats_els;
                if (tel_cycles > 0) {
                    stats_els.push_back(separatorStyled(LIGHT));
                    stats_els.push_back(separatorEmpty());
                    stats_els.push_back(text("  Session stats:") | dim);
                    stats_els.push_back(hbox({ text("    Cycles   ") | dim,
                        text(std::format("{}", tel_cycles)) | color(Color::Cyan) | bold }));
                    stats_els.push_back(hbox({ text("    Stalls   ") | dim,
                        text(std::format("{}", tel_stalls)) | color(Color::Yellow) | bold }));
                    stats_els.push_back(hbox({ text("    Forwards ") | dim,
                        text(std::format("{}", tel_forwards)) | color(Color::GreenLight) | bold }));
                    stats_els.push_back(hbox({ text("    Flushes  ") | dim,
                        text(std::format("{}", tel_flushes)) | color(Color::Red) | bold }));
                } else {
                    stats_els.push_back(text("  (no execution data yet — load a program and step)") | dim);
                }

                auto desc_panel = window(text(desc_title),
                    vbox({
                        separatorEmpty(),
                        paragraph(desc_body) | dim,
                        separatorEmpty(),
                        text(desc_ref) | dim,
                        separatorEmpty(),
                        vbox(stats_els),
                    })
                ) | flex;

                content = hbox({ sel_panel, separatorEmpty(), desc_panel });
            }

            // ══ TAB 3: Program Loader ═════════════════════════════════════════════
            else if (tab_idx == 3) {
                // ── File path row: input + button on the same line ────────────────
                // window() embeds the label in the border — FTXUI dom §window
                auto file_panel = window(text(" File path "),
                    hbox({
                        text(" "),
                        filepath_input->Render() | flex,
                        text(" "),
                        btn_load->Render(),
                        text(" "),
                    })
                );

                // ── Status row ────────────────────────────────────────────────────
                const Color  sc   = loader_ok ? Color::GreenLight : Color::RedLight;
                const char*  sym  = loader_ok ? " ✓  " : " ✗  ";
                Element status_el = hbox({
                    text(sym) | color(sc) | bold,
                    text(loader_status) | color(sc),
                });

                // ── Loaded program preview (up to 12 words from address 0) ────────
                Elements preview;
                int shown = 0;
                for (uint32_t addr = 0; addr < 48 && shown < 12; addr += 4) {
                    auto w = cpu->mem().read_word(addr);
                    if (!w) break;
                    std::string mn = "       ";
                    if (auto d = mips::Decoder::decode(*w))
                        mn = std::format("{:<7}", std::string(mips::Decoder::mnemonic(*d)));
                    bool is_pc = (addr == cpu->pc());
                    Elements row;
                    row.push_back(text(is_pc ? " ▶ " : "   ")
                        | color(Color::CyanLight));
                    row.push_back(text(std::format("{:08X}:", addr))
                        | (is_pc ? (bold | color(Color::Cyan)) : dim));
                    row.push_back(text(std::format(" {:08X}", *w))
                        | (is_pc ? (bold | color(Color::YellowLight)) : color(Color::White)));
                    row.push_back(text("  "));
                    row.push_back(text(mn)
                        | (is_pc ? (bold | color(Color::YellowLight)) : dim));
                    preview.push_back(hbox(row));
                    ++shown;
                }
                if (preview.empty())
                    preview.push_back(text("  (memory empty — no program loaded)") | dim);

                auto preview_panel = window(text(" Loaded program "),
                    vbox(preview) | vscroll_indicator | frame
                ) | flex;

                // ── Format reference panel ─────────────────────────────────────────
                // Keeps the syntax rules visible without switching away
                auto fmt_panel = window(text(" .hex format "),
                    vbox({
                        text(" One 32-bit hex word per line.") | dim,
                        text(" Lines starting with # are ignored.") | dim,
                        text(" No 0x prefix; lowercase ok.") | dim,
                        separatorEmpty(),
                        separatorStyled(LIGHT),
                        separatorEmpty(),
                        text(" Example program:") | dim,
                        separatorEmpty(),
                        text(" # add $t0, $zero, $zero") | color(Color::GrayDark),
                        text(" 00004020") | color(Color::Cyan),
                        text(" # addi $t0, $t0, 5") | color(Color::GrayDark),
                        text(" 21080005") | color(Color::Cyan),
                        text(" # sw $t0, 0($zero)") | color(Color::GrayDark),
                        text(" AC080000") | color(Color::Cyan),
                        text(" # halt (self-jump)") | color(Color::GrayDark),
                        text(" 08000003") | color(Color::Cyan),
                        separatorEmpty(),
                        separatorStyled(LIGHT),
                        separatorEmpty(),
                        text(" [F10] step  [F10 hold] run") | dim,
                        text(" Go to CPU Dashboard tab") | dim,
                        text(" to observe execution.") | dim,
                    })
                ) | size(WIDTH, EQUAL, 36);

                content = vbox({
                    file_panel,
                    status_el,
                    separatorEmpty(),
                    hbox({ preview_panel, separatorEmpty(), fmt_panel }) | flex,
                });
            }

            // ══ TAB 4: Core Pulse ════════════════════════════════════════════════
            // Shows the live datapath trace for the instruction at PC:
            //   IF — what was fetched
            //   ID — register file reads (A/B inputs)
            //   EX/MEM/WB — control signals from last committed instruction
            // In pipelined mode, also shows the stage snapshot row + hazards.
            else if (tab_idx == 4) {
                const auto& ps    = cpu->pipeline_state();
                const bool  is_pl = (cpu_mode == CpuMode::Pipelined);

                const uint32_t fetch_pc  = cpu->pc();
                const auto     fetch_raw = cpu->mem().read_word(fetch_pc);
                const auto     fetch_dec = fetch_raw ? mips::Decoder::decode(*fetch_raw)
                                                     : std::nullopt;

                // ── Core Pulse: reactive flow-field background ──────────────────────
                auto bg_3d = create_datapath_3d_background(
                    global_mouse_x, global_mouse_y, global_mouse_active,
                    cpu->cycle_count());
                Element scope_panel = window(
                    text(" ClearCore "),
                    bg_3d->Render()
                );

                // ── IF: instruction at current PC ────────────────────────────────
                Element if_panel;
                if (!fetch_raw) {
                    if_panel = window(text(" IF  Instruction Fetch "),
                        hbox({ text("  "), text("memory empty — load a program first") | dim })
                    );
                } else {
                    if_panel = window(text(" IF  Instruction Fetch "),
                        vbox({
                            hbox({
                                text("  PC  ") | dim,
                                text(std::format("0x{:08X}", fetch_pc))
                                    | color(Color::Cyan) | bold,
                                text("   Raw  ") | dim,
                                text(std::format("{:08X}", *fetch_raw))
                                    | color(Color::White) | bold,
                            }),
                            hbox({
                                text("  Asm ") | dim,
                                text(fetch_dec
                                    ? reconstruct_asm(*fetch_dec, fetch_pc)
                                    : "(unknown encoding)")
                                    | (fetch_dec ? (color(Color::YellowLight) | bold) : (color(Color::Red))),
                            }),
                        })
                    );
                }

                // ── ID: register file reads ───────────────────────────────────────
                uint8_t nr_rs = 0, nr_rt = 0, nr_rd = 0;
                bool    has_rs = false, has_rt = false, has_rd = false;
                if (fetch_dec) {
                    if (fetch_dec->format == mips::InstrFormat::R) {
                        const auto& r = fetch_dec->r();
                        nr_rs = r.rs; nr_rt = r.rt; nr_rd = r.rd;
                        has_rs = has_rt = has_rd = true;
                    } else if (fetch_dec->format == mips::InstrFormat::I) {
                        const auto& i = fetch_dec->i();
                        nr_rs = i.rs; nr_rt = i.rt;
                        has_rs = has_rt = true;
                    }
                }

                Elements id_els;
                if (has_rs) {
                    const uint32_t v = cpu->regs().read(nr_rs);
                    id_els.push_back(hbox({
                        text(std::format("  A  ${:<6}", kRegNames[nr_rs])) | dim,
                        text(std::format("(#{:<2})  = ", nr_rs)) | color(Color::GrayDark),
                        text(std::format("0x{:08X}", v)) | color(Color::CyanLight) | bold,
                        text(std::format("  ({:+d})", static_cast<int32_t>(v))) | dim,
                    }));
                }
                if (has_rt) {
                    const uint32_t v = cpu->regs().read(nr_rt);
                    id_els.push_back(hbox({
                        text(std::format("  B  ${:<6}", kRegNames[nr_rt])) | dim,
                        text(std::format("(#{:<2})  = ", nr_rt)) | color(Color::GrayDark),
                        text(std::format("0x{:08X}", v)) | color(Color::GreenLight) | bold,
                        text(std::format("  ({:+d})", static_cast<int32_t>(v))) | dim,
                    }));
                }
                if (has_rd) {
                    id_els.push_back(hbox({
                        text(std::format("  WB  ${:<6}", kRegNames[nr_rd])) | dim,
                        text(std::format("(#{:<2})  ← will receive result", nr_rd)) | dim,
                    }));
                }
                if (!has_rs && !has_rt)
                    id_els.push_back(text("  (no register reads — J-type or memory empty)") | dim);

                Element id_panel = window(
                    text(" ID  Register File Read "), vbox(id_els));

                // ── EX/MEM/WB: control signals from last committed instruction ────
                // last_control() returns the Control snapshot set during WB.
                const auto& ctrl = cpu->last_control();
                auto make_sig = [](const char* n, bool v, Color on_col) {
                    return hbox({
                        text(std::format("  {:<8}", n)) | dim,
                        text(v ? "1" : "0")
                            | color(v ? on_col : Color::GrayDark) | bold,
                    });
                };
                Element ctrl_panel = window(
                    text(" EX / MEM / WB  Control signals (last committed instruction) "),
                    hbox({
                        vbox({
                            make_sig("RegWrite", ctrl.reg_write,  Color::GreenLight),
                            make_sig("RegDest",  ctrl.reg_dst,    Color::GreenLight),
                            make_sig("ALUSrc",   ctrl.alu_src,    Color::GreenLight),
                        }) | flex,
                        separatorEmpty(),
                        vbox({
                            make_sig("MemRead",  ctrl.mem_read,   Color::BlueLight),
                            make_sig("MemWrite", ctrl.mem_write,  Color::RedLight),
                            make_sig("Mem2Reg",  ctrl.mem_to_reg, Color::BlueLight),
                        }) | flex,
                        separatorEmpty(),
                        vbox({
                            make_sig("Branch",   ctrl.branch,     Color::Yellow),
                            make_sig("Jump",     ctrl.jump,       Color::Yellow),
                            text(""),
                        }) | flex,
                    })
                );

                // ── Pipeline stage snapshot (pipelined mode only) ─────────────────
                Element stages_panel = emptyElement();
                Element hazard_panel = emptyElement();
                if (is_pl) {
                    Elements stages_row;
                    for (int i = 0; i < 5; ++i) {
                        const auto& st  = ps.stages[i];
                        const Color col = stage_accent(i, st);
                        std::string mn  = "─────";
                        if (st.valid && st.raw != 0) {
                            if (auto d = mips::Decoder::decode(st.raw))
                                mn = std::string(mips::Decoder::mnemonic(*d));
                            else mn = "???";
                        }
                        const char* state_lbl =
                            st.flushed ? "flush"
                          : st.stalled ? "stall"
                          : st.valid   ? "valid"
                          : "empty";
                        stages_row.push_back(
                            window(
                                text(std::format(" {} ", st.name)) | color(col) | bold,
                                vbox({
                                    text(std::format("  {:08X}", st.pc))
                                        | (st.valid ? color(Color::Cyan) : dim),
                                    text(std::format("  {}", mn))
                                        | color(col) | bold,
                                    text(std::format("  {}", state_lbl)) | dim,
                                })
                            ) | flex
                        );
                        if (i < 4)
                            stages_row.push_back(text(" ─► ") | dim | vcenter);
                    }
                    stages_panel = window(text(" Pipeline stage snapshot "),
                        hbox(stages_row));

                    // Hazard / forwarding row
                    const bool any_fwd =
                        ps.fwd_ex_to_ex_a  || ps.fwd_ex_to_ex_b ||
                        ps.fwd_mem_to_ex_a || ps.fwd_mem_to_ex_b;
                    Elements hz;
                    hz.push_back(hbox({
                        text("  Load stall    ") | dim,
                        text(ps.load_stall ? "YES" : "no")
                            | color(ps.load_stall ? Color::Yellow : Color::GrayDark) | bold,
                        text("    Branch flush  ") | dim,
                        text(ps.branch_flush ? "YES" : "no")
                            | color(ps.branch_flush ? Color::Red : Color::GrayDark) | bold,
                    }));
                    hz.push_back(hbox({
                        text("  Forwarding    ") | dim,
                        text(any_fwd ? "active" : "none")
                            | color(any_fwd ? Color::GreenLight : Color::GrayDark) | bold,
                        any_fwd ? (hbox({
                            text("   "),
                            text(ps.fwd_ex_to_ex_a || ps.fwd_ex_to_ex_b
                                ? " EX/MEM→EX " : "")
                                | color(Color::GreenLight) | dim,
                            text(ps.fwd_mem_to_ex_a || ps.fwd_mem_to_ex_b
                                ? " MEM/WB→EX " : "")
                                | color(Color::GreenLight) | dim,
                        })) : emptyElement(),
                    }));
                    hazard_panel = window(text(" Hazard & forwarding status "), vbox(hz));
                }

                // ── Assemble page ─────────────────────────────────────────────────
                Elements page;
                page.push_back(scope_panel);
                page.push_back(separatorEmpty());
                page.push_back(if_panel);
                page.push_back(separatorEmpty());
                page.push_back(id_panel);
                page.push_back(separatorEmpty());
                if (is_pl) {
                    page.push_back(stages_panel);
                    page.push_back(separatorEmpty());
                    page.push_back(hazard_panel);
                    page.push_back(separatorEmpty());
                }
                page.push_back(ctrl_panel);
                content = vbox(page);
            }
            // ══ TAB 5: Placeholder ══════════════════════════════════════════════════════
            else if (tab_idx == 5) {
                 content = window(text(" Utility Tools Panel "), vbox({
                    text("This tab will contain various utilities such as debugger tools, profiling data viewers, or network simulation interfaces.") | dim,
                    separatorEmpty(),
                    text("Feature development pending...") | center | dim,
                }));
            }

            // ── Root chrome ───────────────────────────────────────────────────────
            Elements header;
            header.push_back(text(" ClearCore MIPS") | bold | color(Color::Cyan));
            header.push_back(filler());
            header.push_back(tab_menu->Render());

            Elements root;
            root.push_back(hbox(header));
            root.push_back(separator());
            root.push_back(content | flex);
            root.push_back(separator());
            // ── Status bar: key hints left · CPU mode badge + cycle count right ──
            {
                const bool is_pl = (cpu_mode == CpuMode::Pipelined);
                Elements sb;
                sb.push_back(text(" "));
                sb.push_back(text("[Tab]")     | bold); sb.push_back(text(" Navigate  ")  | dim);
                sb.push_back(text("[F10]")     | bold); sb.push_back(text(" Step  ")      | dim);
                sb.push_back(text("[PgUp/Dn]") | bold); sb.push_back(text(" Scroll  ")   | dim);
                sb.push_back(text("[Home]")    | bold); sb.push_back(text(" Snap  ")      | dim);
                sb.push_back(text("[Esc]")     | bold); sb.push_back(text(" Quit")        | dim);
                sb.push_back(filler());
                sb.push_back(
                    text(is_pl ? " PIPE " : "  SC  ") | bold
                    | color(Color::Black)
                    | bgcolor(is_pl ? Color::YellowLight : Color::GreenLight)
                );
                // Braille spinner: animates while running, still dot while paused.
                static constexpr std::array<const char*, 8> kSpin =
                    {"⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧"};
                const bool running = auto_run.load();
                const char* sp = running
                    ? kSpin[anim_frame % kSpin.size()]
                    : "⠿";
                sb.push_back(text(std::format("  {} ", sp))
                    | color(running ? Color::CyanLight : Color::GrayDark));
                sb.push_back(text("cycle ") | dim);
                sb.push_back(text(std::format("{}", cpu->cycle_count()))
                    | color(Color::Cyan) | bold);
                sb.push_back(text("  "));
                root.push_back(hbox(sb));
            }
            return vbox(root) | border;
        });

        // ── Event handler ─────────────────────────────────────────────────────────
        // Approximate top row of the Core Pulse panel's interior (title bar +
        // tab strip + separator above it). Used only to decide whether the
        // mouse is "inside" the panel — not for pixel-perfect hit testing.
        constexpr int kCorePulseTopRow = 3;

        Component root = CatchEvent(renderer, [&](const Event &event) {
            // ── Mouse tracking — only active while inside the Core Pulse panel ──────
            if (event.is_mouse()) {
                auto& evt  = const_cast<Event&>(event);
                const int mx = evt.mouse().x;
                const int my = evt.mouse().y;

                const auto term       = Terminal::Size();
                const int  panel_rows = std::clamp(term.dimy / 3, 14, 26);
                const int  panel_top  = kCorePulseTopRow;
                const int  panel_bot  = panel_top + panel_rows + 2;  // +2 ≈ borders

                global_mouse_active = (tab_idx == 4) &&
                                       my >= panel_top && my <= panel_bot &&
                                       mx >= 0 && mx <= term.dimx;

                if (global_mouse_active) {
                    global_mouse_x = std::clamp(mx, 0, term.dimx);
                    global_mouse_y = std::clamp(my - panel_top, 0, panel_rows);
                }
            }
            // ── Custom event handler logic ────────────────────────────────────────
            if (event == Event::Custom) {
                if (auto_run.load()) do_step();
                return true;
            }
            if (event == Event::Escape ||
                event == Event::Character(static_cast<char>(3))) {
                screen.Exit(); return true;
            }
            if (event == Event::F10 && tab_idx == 1) { do_step(); return true; }
            if (tab_idx == 1) {
                if (event == Event::PageUp)   { mem_base = (mem_base >= 32) ? mem_base - 32 : 0; return true; }
                if (event == Event::PageDown) { mem_base += 32; return true; }
                if (event == Event::Home)     { mem_base = cpu->pc() & ~uint32_t{0x1F}; return true; }
            }
            return false;
        });

        screen.Loop(root);
        app_live.store(false);
        auto_run.store(false);
        ticker.join();
        return 0;
    }

} // namespace nsc