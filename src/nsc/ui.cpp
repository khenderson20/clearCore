#include "nsc/ui.h"
#include "nsc/converter.h"
#include "mips/processor.h"
#include "mips/single_cycle_cpu.h"
#include "mips/pipelined_cpu.h"
#include "mips/decoder.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <array>
#include <atomic>
#include <chrono>
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
static bool load_hex_file(const std::string& path, mips::IProcessor& cpu,
                          std::string& msg) {
    std::ifstream f(path);
    if (!f.is_open()) { msg = std::format("Cannot open '{}'", path); return false; }
    std::vector<uint32_t> prog;
    std::string line;
    int n = 0;
    while (std::getline(f, line)) {
        ++n;
        auto cp = line.find('#');
        if (cp != std::string::npos) line = line.substr(0, cp);
        std::string clean;
        for (char c : line) if (!std::isspace(c)) clean += c;
        if (clean.empty()) continue;
        try {
            size_t used = 0;
            prog.push_back(static_cast<uint32_t>(std::stoul(clean, &used, 16)));
        } catch (...) { msg = std::format("Bad hex on line {}", n); return false; }
    }
    if (cpu.load_program(prog, 0)) { msg = std::format("Loaded {} words.", prog.size()); return true; }
    msg = "Program too large for memory.";
    return false;
}

// ─── Assembly mnemonic reconstruction ─────────────────────────────────────────
// Produces proper lowercase MIPS assembly (e.g. "add $t2, $t0, $t1").
// Used in the instruction decode panel and execution trace.
static std::string reconstruct_asm(const mips::DecodedInstr& dec, uint32_t pc) {
    // Lowercase mnemonic
    std::string mn;
    for (char c : mips::Decoder::mnemonic(dec))
        mn += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (dec.format == mips::InstrFormat::R) {
        const auto& r = dec.r();
        const auto rd = kRegNames[r.rd], rs = kRegNames[r.rs], rt = kRegNames[r.rt];
        switch (r.funct) {
            case mips::FunctCode::SLL:  case mips::FunctCode::SRL:
            case mips::FunctCode::SRA:
                return std::format("{} ${}, ${}, {}", mn, rd, rt, r.shamt);
            case mips::FunctCode::SLLV: case mips::FunctCode::SRLV:
                return std::format("{} ${}, ${}, ${}", mn, rd, rt, rs);
            case mips::FunctCode::JR:
                return std::format("{} ${}", mn, rs);
            case mips::FunctCode::JALR:
                return std::format("{} ${}, ${}", mn, rd, rs);
            default:
                return std::format("{} ${}, ${}, ${}", mn, rd, rs, rt);
        }
    }
    if (dec.format == mips::InstrFormat::I) {
        const auto& i = dec.i();
        const auto rs = kRegNames[i.rs], rt = kRegNames[i.rt];
        int32_t simm = mips::Decoder::sign_extend(i.imm);
        switch (dec.opcode) {
            case mips::Opcode::LW: case mips::Opcode::LBU:
            case mips::Opcode::LHU: case mips::Opcode::SW:
                return std::format("{} ${}, {}(${})", mn, rt, simm, rs);
            case mips::Opcode::LUI:
                return std::format("{} ${}, 0x{:04X}", mn, rt, i.imm);
            case mips::Opcode::BEQ: case mips::Opcode::BNE:
                return std::format("{} ${}, ${}, {:+d}", mn, rs, rt, simm);
            default:
                return std::format("{} ${}, ${}, {:+d}", mn, rt, rs, simm);
        }
    }
    if (dec.format == mips::InstrFormat::J) {
        uint32_t jaddr = ((pc + 4) & 0xF000'0000u) | (dec.j().target << 2);
        return std::format("{} 0x{:08X}", mn, jaddr);
    }
    return mn + " ???";
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

// ─── runApp ───────────────────────────────────────────────────────────────────
int runApp() {

    // ── Core state ────────────────────────────────────────────────────────────
    Converter converter;
    CpuMode cpu_mode = CpuMode::SingleCycle;
    std::unique_ptr<mips::IProcessor> cpu = std::make_unique<mips::SingleCycleCpu>();

    // Auto-run: background thread posts Event::Custom; step runs on UI thread
    std::atomic<bool> auto_run{false};
    std::atomic<bool> app_live{true};
    int speed_ms = 200;  // plain int for Slider (int*)

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
    int tab_idx = 0;
    std::vector<std::string> tab_labels = {
        " 🔢 Converter ", " 📊 CPU Dashboard ", " 🧩 CPU Config ", " 📝 Program Loader ", " 💾 Datapath View",
        "  Utility Tools", // Added Tab 5 label for consistency, although content is placeholder
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
        while (app_live.load()) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(std::max(10, speed_ms)));
            if (auto_run.load())
                screen.PostEvent(Event::Custom);
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
    Component cpu_selector = Radiobox(&cpu_mode_names, &cpu_mode_idx);
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

    Component main_container = Container::Vertical({
        tab_menu,
        Container::Tab({
            conv_container, ctrl_container, cfg_container, loader_container,
            // Added Datapath View (Index 4) and Utility Tools (Index 5)
            // We need to ensure the tab structure matches tab_labels size
        }, &tab_idx),
    });

    // ── Renderer ──────────────────────────────────────────────────────────────
    Component renderer = Renderer(main_container, [&] {
        Element content;

        // ══ TAB 0: Converter ══════════════════════════════════════════════════
        if (tab_idx == 0) {
            auto v32 = static_cast<uint32_t>(converter.value() & 0xFFFF'FFFFu);
            auto field_box = [&](int hi, int lo, Color col, const char* rng, const char* lbl) {
                uint32_t mask = (hi < 31 || lo > 0) ? ((1u << (hi-lo+1))-1u) : 0xFFFFFFFFu;
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

            content = vbox({
                hbox({text(" DEC: "), dec_input->Render() | flex}) | border,
                hbox({text(" HEX: "), hex_input->Render() | flex}) | border,
                hbox({text(" BIN: "), bin_input->Render() | flex}) | border,
                separator(),
                vbox({
                    hbox({text(" Nibble:  ") | dim, text(nibble_str) | color(Color::Cyan) | bold}),
                    hbox({text(" Decode:  ") | dim, text(mnem_str)   | color(Color::YellowLight) | bold}),
                }) | border,
                separator(),
                window(text(" R-format field breakdown (32-bit) "), hbox(bits_row)),
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
            Element pipeline_panel = window(
                text(cpu_mode == CpuMode::Pipelined
                     ? " Pipeline  IF → ID → EX → MEM → WB "
                     : " Execution State "),
                vbox({header_bar, separatorEmpty(),
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

        // ══ TAB 2: Config ══════════════════════════════════════════════════════
        else if (tab_idx == 2) {
            content = window(text(" CPU Implementation "), vbox({
                text("Select CPU implementation:") | dim,
                separatorEmpty(),
                cpu_selector->Render(),
                separatorEmpty(),
                btn_apply->Render() | center,
                separatorEmpty(),
                text(" Single-Cycle  one instruction per cycle (H&H Ch. 7)") | dim,
                text(" Pipelined     5-stage with forwarding & hazard detection (H&H Ch. 8)") | dim,
            }));
        }

        // ══ TAB 3: Loader ══════════════════════════════════════════════════════
        else if (tab_idx == 3) {
            content = window(text(" Program Loader (.hex) "), vbox({
                text("One 32-bit hex word per line.  Lines starting with '#' are comments.") | dim,
                separatorEmpty(),
                hbox({text(" File: "), filepath_input->Render() | flex}) | border,
                separatorEmpty(),
                btn_load->Render() | size(WIDTH, LESS_THAN, 14),
                separatorEmpty(),
                hbox({
                    text(" Status: ") | dim,
                    text(loader_status) | bold
                        | color(loader_ok ? Color::GreenLight : Color::RedLight),
                }),
            }));
        }

        // ══ TAB 4: Datapath View ═════════════════════════════════════════════════
        else if (tab_idx == 4) {
            uint32_t pc = cpu->pc();
            auto word_opt = cpu->mem().read_word(pc);

            Elements instruction_memory_content;
            std::string memory_text = "No Instruction";
            if (word_opt) {
                uint32_t raw_instr = *word_opt;
                instruction_memory_content.push_back(text(std::format("0x{:08X}", raw_instr)) | bold);
                memory_text = std::format("0x{:08X}", raw_instr);
            }

            // FIX: Grouped the PC text into an hbox, and passed the vector into its own vbox
            Element instruction_memory = window(text(" Instruction Memory "), vbox({
                hbox({text(" PC: 0x") | dim, text(std::format("{:08X}", pc)) | bold | color(Color::Cyan)}),
                separator(),
                vbox(instruction_memory_content)
            }) | flex);

            Element register_file = window(text(" Register File (32 Regs) "), text("(Rs/Rt Read, Rd Write)") | dim);
            Element alu = window(text(" ALU "), text("(Calculation / Address Gen)") | dim);
            Element data_memory = window(text(" Data Memory "), text("(Read/Write Word)") | dim);

            Elements flow;
            // FIX: Moved | flex outside of the hbox initializer list
            flow.push_back(hbox({instruction_memory}) | flex); // Fetch Stage visualization placeholder

            // Path 1: Instruction fields to Register File inputs (Rs, Rt)
            Elements regfile_input_path;
            if (auto current_instr = mips::Decoder::decode(*word_opt)) {
                const auto& dec = *current_instr;
                uint32_t rs_idx = 0, rt_idx = 0;

                // Determine Rs and Rt indices based on format (R or I)
                if (dec.format == mips::InstrFormat::R) {
                    rs_idx = dec.r().rs;
                    rt_idx = dec.r().rt;
                } else if (dec.format == mips::InstrFormat::I) {
                    rs_idx = dec.i().rs;
                    rt_idx = dec.i().rt;
                }

                regfile_input_path.push_back(text("IR[Rs] → ") | dim);
                regfile_input_path.push_back(text(std::format("{:<4}", kRegNames[rs_idx])) | bold | color(Color::CyanLight));
                regfile_input_path.push_back(text(" | ") | dim);
                regfile_input_path.push_back(text("IR[Rt] → ") | dim);
                regfile_input_path.push_back(text(std::format("{:<4}", kRegNames[rt_idx])) | bold | color(Color::GreenLight));
            } else {
                 regfile_input_path.push_back(text("IR[...] → ") | dim);
                 // FIX: Escaped the third question mark to prevent C++ trigraph interpretation
                 regfile_input_path.push_back(text("(??\?)") | dim);
                 regfile_input_path.push_back(text(" | ") | dim);
                 regfile_input_path.push_back(text("IR[...] → ") | dim);
                 regfile_input_path.push_back(text("(??\?)") | dim);
            }

            // Path 2: Register File outputs and immediate/ALU inputs
            Elements alu_input_mux;
            alu_input_mux.push_back(text("RF Out / Imm → ") | dim);
            alu_input_mux.push_back(text("(A)") | dim);
            alu_input_mux.push_back(text(" + ") | dim);
            alu_input_mux.push_back(text("(B)") | dim);

            // Data flow through ALU and Memory/Write Back
            Elements data_flow;
            data_flow.push_back(text("ALU Result → ") | dim);
            data_flow.push_back(text("(Address / Data)") | dim);
            data_flow.push_back(text(" + ") | dim);
            data_flow.push_back(text("Mem Read Data → ") | dim);
            data_flow.push_back(text("(WB Data)") | dim);

            Elements datapath_vbox;
            // FIX: Moved | flex outside of the hbox initializer list
            datapath_vbox.push_back(hbox({instruction_memory}) | flex);
            datapath_vbox.push_back(separator());

            // FIX: Capitalized 'Element', removed the 's' where a single node is expected,
            // and wrapped existing Elements (vectors) in an hbox to prevent initializer list conflicts.
            Element regfile_flow = hbox({register_file, separatorEmpty(), filler()});
            Element datapath_stage1 = hbox({hbox(regfile_input_path), separatorEmpty(), filler()});
            datapath_vbox.push_back(datapath_stage1);

            Element datapath_stage2 = hbox({hbox(alu_input_mux), separatorEmpty(), filler()});
            datapath_vbox.push_back(datapath_stage2);

            Element datapath_stage3 = hbox(data_flow) | flex;
            datapath_vbox.push_back(datapath_stage3);

            datapath_vbox.push_back(separator());
            // FIX: Moved | flex outside of the hbox initializer list
            datapath_vbox.push_back(hbox({alu}) | flex); // ALU block representation
            datapath_vbox.push_back(separator());
            datapath_vbox.push_back(hbox({data_memory}) | flex); // Data Memory block representation

            content = window(text(" MIPS Datapath Simulation "), vbox({
                text("Visual representation of the CPU's data flow components (Simplified Single-Cycle Flow):") | dim,
                separatorEmpty(),
                vbox(datapath_vbox) | flex
            }));
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
        header.push_back(text(" 🧠 ClearCore MIPS") | bold | color(Color::Cyan));
        header.push_back(filler());
        header.push_back(tab_menu->Render());

        Elements root;
        root.push_back(hbox(header));
        root.push_back(separator());
        root.push_back(content | flex);
        root.push_back(separator());
        root.push_back(
            text(" Tab: Navigate  •  F10: Step  •  PgUp/Dn: Mem scroll  •  Home: Snap to PC  •  Esc: Quit")
            | dim | center
        );
        return vbox(root) | border;
    });

    // ── Event handler ─────────────────────────────────────────────────────────
    Component root = CatchEvent(renderer, [&](const Event &event) {
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