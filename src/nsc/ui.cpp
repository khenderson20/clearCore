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

#include <string>
#include <vector>
#include <fstream>
#include <format>
#include <sstream>
#include <memory>

namespace nsc {

using namespace ftxui;

// ─── CPU implementation selector ──────────────────────────────────────────────
enum class CpuMode {
    SingleCycle,
    Pipelined,
};

// ─── Helper: Read a file containing 32-bit hex words ────────────────────────
static bool load_hex_file(const std::string& path, mips::IProcessor& cpu,
                          std::string& status_msg) {
    std::ifstream file(path);
    if (!file.is_open()) {
        status_msg = "Error: Could not open file '" + path + "'";
        return false;
    }

    std::vector<uint32_t> program;
    std::string line;
    int line_num = 0;

    while (std::getline(file, line)) {
        line_num++;
        // Strip comments and whitespace
        auto comment_pos = line.find('#');
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }

        std::string cleaned;
        for (char c : line) if (!std::isspace(c)) cleaned += c;
        if (cleaned.empty()) continue;

        try {
            size_t consumed = 0;
            uint32_t word = std::stoul(cleaned, &consumed, 16);
            program.push_back(word);
        } catch (...) {
            status_msg = std::format("Error: Invalid hex at line {}", line_num);
            return false;
        }
    }

    if (cpu.load_program(program, 0x00000000)) {
        status_msg = std::format("Success: Loaded {} instructions.", program.size());
        return true;
    } else {
        status_msg = "Error: Program does not fit in memory.";
        return false;
    }
}

// ─── Pipeline stage visualization (WebRISC-V style) ───────────────────────────
// Returns a visual representation of which stage each instruction is in.
static std::string pipeline_diagram(const mips::PipelineState& ps) {
    std::string diagram;
    const auto& stages = ps.stages;

    // Header
    diagram += "Pipeline State (IF → ID → EX → MEM → WB):\n";

    // Each stage
    for (int i = 0; i < 5; ++i) {
        const auto& snap = stages[i];

        // Stage name and status
        if (snap.valid) {
            diagram += std::format("  {}: ", snap.name);

            // Show the instruction at this stage
            if (snap.raw != 0) {
                if (auto dec = mips::Decoder::decode(snap.raw)) {
                    diagram += std::string(mips::Decoder::mnemonic(*dec));
                } else {
                    diagram += "???";
                }
            }

            // Status indicators
            if (snap.stalled) diagram += " [STALL]";
            if (snap.flushed) diagram += " [FLUSH]";

            diagram += "\n";
        } else {
            diagram += std::format("  {}: (empty)\n", snap.name);
        }
    }

    // Forwarding paths (only for pipelined)
    if (ps.fwd_ex_to_ex_a || ps.fwd_ex_to_ex_b ||
        ps.fwd_mem_to_ex_a || ps.fwd_mem_to_ex_b) {
        diagram += "  Forwarding: ";
        if (ps.fwd_ex_to_ex_a || ps.fwd_ex_to_ex_b)
            diagram += "EX/MEM→EX ";
        if (ps.fwd_mem_to_ex_a || ps.fwd_mem_to_ex_b)
            diagram += "MEM/WB→EX ";
        diagram += "\n";
    }

    if (ps.load_stall) diagram += "  ⚠ Load-use stall active\n";
    if (ps.branch_flush) diagram += "  ⚠ Branch flush in progress\n";

    return diagram;
}

int runApp() {
    // ─────────────────────────────────────────────────────────────────────────
    // Core State
    // ─────────────────────────────────────────────────────────────────────────
    Converter converter;
    CpuMode cpu_mode = CpuMode::SingleCycle;
    std::unique_ptr<mips::IProcessor> cpu = std::make_unique<mips::SingleCycleCpu>();

    bool syncing = false;
    int tab_index = 0;
    std::vector<std::string> tab_entries = {
        " 🔢 Converter ",
        " 📊 CPU Dashboard ",
        " ⚙️ CPU Config ",
        " 📝 Program Loader "
    };

    // UI View State (Converter)
    std::string dec_str = "0";
    std::string hex_str = "0";
    std::string bin_str = "0";
    std::string grouped_bin_str = "0000";
    std::string mips_mnemonic_str = "nop";

    // UI View State (Config)
    std::vector<std::string> cpu_modes = { "Single-Cycle", "Pipelined (5-stage)" };
    int cpu_mode_index = 0;

    // UI View State (Loader)
    std::string filepath_str = "program.hex";
    std::string loader_status_str = "Ready.";

    // ─────────────────────────────────────────────────────────────────────────
    // Converter Synchronization Logic
    // ─────────────────────────────────────────────────────────────────────────
    auto update_views = [&](Converter::Base trigger_base, const std::string& input_str) {
        if (syncing) return;
        syncing = true;

        if (converter.update(input_str, trigger_base)) {
            if (trigger_base != Converter::Base::Decimal) dec_str = converter.as(Converter::Base::Decimal);
            if (trigger_base != Converter::Base::Hex)     hex_str = converter.as(Converter::Base::Hex);
            if (trigger_base != Converter::Base::Binary)  bin_str = converter.as(Converter::Base::Binary);

            grouped_bin_str = converter.bits();

            uint64_t current_val = converter.value();
            if (current_val <= 0xFFFFFFFF) {
                auto decoded = mips::Decoder::decode(static_cast<uint32_t>(current_val));
                mips_mnemonic_str = decoded.has_value()
                                    ? std::string(mips::Decoder::mnemonic(decoded.value()))
                                    : "Invalid Instruction";
            } else {
                mips_mnemonic_str = "Out of 32-bit range";
            }
        }
        syncing = false;
    };

    // ─────────────────────────────────────────────────────────────────────────
    // Components: Converter (Tab 0)
    // ─────────────────────────────────────────────────────────────────────────
    InputOption dec_options;
    dec_options.on_change = [&] { update_views(Converter::Base::Decimal, dec_str); };
    Component dec_input = Input(&dec_str, "0", dec_options);

    InputOption hex_options;
    hex_options.on_change = [&] { update_views(Converter::Base::Hex, hex_str); };
    Component hex_input = Input(&hex_str, "0", hex_options);

    InputOption bin_options;
    bin_options.on_change = [&] { update_views(Converter::Base::Binary, bin_str); };
    Component bin_input = Input(&bin_str, "0", bin_options);

    Component converter_container = Container::Vertical({dec_input, hex_input, bin_input});

    // ─────────────────────────────────────────────────────────────────────────
    // Components: CPU Controls (Tab 1)
    // ─────────────────────────────────────────────────────────────────────────
    Component btn_step = Button(" Step (F10) ", [&] { cpu->step(); });
    Component btn_run = Button(" Run ", [&] { cpu->run(100'000); });
    Component btn_reset = Button(" Reset ", [&] {
        cpu->reset(false);
        loader_status_str = "CPU Reset.";
    });
    Component controls_container = Container::Horizontal({btn_step, btn_run, btn_reset});

    // ─────────────────────────────────────────────────────────────────────────
    // Components: CPU Config (Tab 2)
    // ─────────────────────────────────────────────────────────────────────────
    Component cpu_mode_selector = Radiobox(&cpu_modes, &cpu_mode_index);

    Component btn_apply_mode = Button(" Apply Mode ", [&] {
        cpu->reset(true);  // Clear everything
        if (cpu_mode_index == 0) {
            cpu = std::make_unique<mips::SingleCycleCpu>();
            cpu_mode = CpuMode::SingleCycle;
            loader_status_str = "Switched to Single-Cycle CPU.";
        } else {
            cpu = std::make_unique<mips::PipelinedCpu>();
            cpu_mode = CpuMode::Pipelined;
            loader_status_str = "Switched to Pipelined CPU (5-stage).";
        }
    });

    Component config_container = Container::Vertical({cpu_mode_selector, btn_apply_mode});

    // ─────────────────────────────────────────────────────────────────────────
    // Components: Loader (Tab 3)
    // ─────────────────────────────────────────────────────────────────────────
    Component filepath_input = Input(&filepath_str, "path/to/program.hex");
    Component btn_load = Button(" Load File ", [&] {
        cpu->reset(true);  // Clear old state
        load_hex_file(filepath_str, *cpu, loader_status_str);
    });
    Component loader_container = Container::Vertical({filepath_input, btn_load});

    // ─────────────────────────────────────────────────────────────────────────
    // Main UI Routing & Layout
    // ─────────────────────────────────────────────────────────────────────────
    MenuOption tab_options = MenuOption::HorizontalAnimated();
    tab_options.entries_option.transform = [](const EntryState& state) {
        Element e = text(state.label) | center;
        if (state.active) {
            e = e | bold | color(Color::Cyan);
        }
        if (state.focused) {
            e = e | inverted;
        }
        return e | size(WIDTH, GREATER_THAN, 16);
    };

    Component tab_selection = Menu(&tab_entries, &tab_index, tab_options);

    Component main_container = Container::Vertical({
        tab_selection,
        Container::Tab({
            converter_container,
            controls_container,
            config_container,
            loader_container
        }, &tab_index)
    });

    Component renderer = Renderer(main_container, [&] {
        Element current_view;

        if (tab_index == 0) {
            // --- TAB 0: CONVERTER ---
            current_view = [&]() {
                Elements e;

                Elements dec_h;
                dec_h.push_back(text(" DEC: "));
                dec_h.push_back(dec_input->Render() | flex);
                e.push_back(hbox(dec_h) | border);

                Elements hex_h;
                hex_h.push_back(text(" HEX: "));
                hex_h.push_back(hex_input->Render() | flex);
                e.push_back(hbox(hex_h) | border);

                Elements bin_h;
                bin_h.push_back(text(" BIN: "));
                bin_h.push_back(bin_input->Render() | flex);
                e.push_back(hbox(bin_h) | border);

                e.push_back(separator());

                Elements nibble_e;
                nibble_e.push_back(text(" Nibble View:") | dim);
                nibble_e.push_back(text(" " + grouped_bin_str) | color(Color::Cyan));
                nibble_e.push_back(text(""));
                nibble_e.push_back(text(" Live MIPS Decode (32-bit):") | dim);
                nibble_e.push_back(text(" " + mips_mnemonic_str) | color(Color::Yellow) | bold);
                e.push_back(vbox(nibble_e) | border);

                return vbox(e);
            }();
        }
        else if (tab_index == 1) {
            // --- TAB 1: CPU DASHBOARD ---
            const mips::PipelineState& ps = cpu->pipeline_state();

            // 1. Render Registers
            Elements col1, col2;
            for (int i = 0; i < 32; ++i) {
                auto reg_txt = text(std::format("${:<2} 0x{:08X}", i, cpu->regs().read(i)));
                if (i != 0 && i == cpu->regs().last_written()) {
                    reg_txt = reg_txt | bold | color(Color::Green);
                }
                if (i < 16) col1.push_back(reg_txt);
                else        col2.push_back(reg_txt);
            }
            auto reg_panel = window(text(" Registers "), [&]() {
                Elements e;
                e.push_back(vbox(col1) | flex);
                e.push_back(separatorEmpty());
                e.push_back(vbox(col2) | flex);
                return hbox(e);
            }());

            // 2. Render Datapath / Execution State
            uint32_t pc = cpu->pc();
            std::string current_instr = "???";
            std::string raw_hex = "00000000";

            if (auto fetched = cpu->mem().read_word(pc)) {
                raw_hex = std::format("{:08X}", *fetched);
                if (auto decoded = mips::Decoder::decode(*fetched)) {
                    current_instr = std::string(mips::Decoder::mnemonic(*decoded));
                }
            }

            auto datapath_panel = window(text(" Datapath "), [&]() {
                Elements e;
                e.push_back(text(std::format("PC: 0x{:08X}", pc)) | bold | color(Color::Cyan));
                e.push_back(separatorLight());
                e.push_back(text("Current Instruction:") | dim);
                e.push_back(text(std::format("{} (0x{})", current_instr, raw_hex)) | bold);
                e.push_back(separatorEmpty());
                e.push_back(text(std::format("CPU Mode: {}",
                    cpu_mode == CpuMode::SingleCycle ? "Single-Cycle" : "Pipelined (5-stage)")) | dim);
                e.push_back(text(std::format("Cycle: {}", cpu->cycle_count())));
                return vbox(e);
            }());

            // 3. Render Pipeline State (if pipelined)
            Element pipeline_view;
            if (cpu_mode == CpuMode::Pipelined) {
                std::string pipeline_text = pipeline_diagram(ps);
                pipeline_view = window(text(" Pipeline Visualization "),
                    text(pipeline_text) | color(Color::Cyan));
            } else {
                pipeline_view = window(text(" Pipeline "),
                    text("Single-cycle mode: no pipeline stages to display"));
            }

            // 4. Render Memory (Quick preview around PC)
            Elements mem_lines;
            for (uint32_t offset = 0; offset <= 16; offset += 4) {
                uint32_t addr = (pc >= 8 ? pc - 8 : 0) + offset;
                if (auto val = cpu->mem().read_word(addr)) {
                    auto line = text(std::format("0x{:08X}: {:08X}", addr, *val));
                    if (addr == pc) line = line | bold | color(Color::Cyan);
                    mem_lines.push_back(line);
                }
            }
            auto mem_panel = window(text(" Memory Preview "), vbox(mem_lines));

            current_view = [&]() {
                Elements dashboard_els;

                Elements hbox_els;
                hbox_els.push_back(reg_panel | size(WIDTH, GREATER_THAN, 30));

                Elements vbox_els;
                vbox_els.push_back(datapath_panel);
                vbox_els.push_back(pipeline_view);
                vbox_els.push_back(mem_panel);
                hbox_els.push_back(vbox(vbox_els) | flex);

                dashboard_els.push_back(hbox(hbox_els) | flex);
                dashboard_els.push_back(window(text(" Controls "), controls_container->Render() | center));

                return vbox(dashboard_els);
            }();
        }
        else if (tab_index == 2) {
            // --- TAB 2: CPU CONFIG ---
            current_view = window(text(" CPU Implementation "), [&]() {
                Elements e;
                e.push_back(text("Select CPU implementation:") | dim);
                e.push_back(separatorEmpty());
                e.push_back(cpu_mode_selector->Render());
                e.push_back(separatorEmpty());
                e.push_back(btn_apply_mode->Render() | center);
                e.push_back(separatorEmpty());
                e.push_back(text(" Single-Cycle: one instruction per cycle (H&H Ch. 7)") | dim);
                e.push_back(text(" Pipelined: 5-stage pipeline with forwarding & hazard") | dim);
                e.push_back(text("            detection (H&H Ch. 8)") | dim);
                return vbox(e);
            }());
        }
        else if (tab_index == 3) {
            // --- TAB 3: LOADER ---
            current_view = window(text(" Program Loader (.hex) "), [&]() {
                Elements e;
                e.push_back(text("Enter path to a file containing hex machine code (one word per line)."));
                e.push_back(text("Lines starting with '#' are ignored.") | dim);
                e.push_back(separatorEmpty());

                Elements hbox_e;
                hbox_e.push_back(text(" File: "));
                hbox_e.push_back(filepath_input->Render() | flex);
                e.push_back(hbox(hbox_e) | border);

                e.push_back(separatorEmpty());
                e.push_back(btn_load->Render() | size(WIDTH, LESS_THAN, 20));
                e.push_back(separatorEmpty());
                e.push_back(text(" Status: " + loader_status_str) | bold);
                return vbox(e);
            }());
        }

        // --- ROOT LAYOUT ---
        return [&]() {
            Elements root_e;

            Elements header_e;
            header_e.push_back(text(" ⚙️ ClearCore MIPS Emulator ") | bold);
            header_e.push_back(filler());
            header_e.push_back(tab_selection->Render());
            root_e.push_back(hbox(header_e));

            root_e.push_back(separator());
            root_e.push_back(current_view | flex);
            root_e.push_back(separator());
            root_e.push_back(text(" [Tab] Navigate • [Enter] Activate • [F10] Step • [Esc/Ctrl+C] Quit") | dim | center);

            return vbox(root_e) | border | flex;
        }();
    });

    // ─────────────────────────────────────────────────────────────────────────
    // Global Event Handler
    // ─────────────────────────────────────────────────────────────────────────
    auto screen = ScreenInteractive::Fullscreen();

    Component event_catcher = CatchEvent(renderer, [&](Event event) {
        if (event == Event::Escape || event == Event::Character(static_cast<char>('c' - 'a' + 1))) {
            screen.Exit();
            return true;
        }
        // Global hotkeys
        if (event == Event::F10 && tab_index == 1) {
            cpu->step();
            return true;
        }
        return false;
    });

    screen.Loop(event_catcher);
    return 0;
}

} // namespace nsc