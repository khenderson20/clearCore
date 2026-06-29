#include "nsc/ui.h"
#include "nsc/converter.h"
#include "mips/decoder.h"
#include "mips/cpu.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <string>
#include <vector>
#include <fstream>
#include <format>
#include <sstream>

namespace nsc {

using namespace ftxui;

// Helper: Read a file containing 32-bit hex words (one per line, ignores '#' comments)
static bool load_hex_file(const std::string& path, mips::Cpu& cpu, std::string& status_msg) {
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

    if (cpu.load_program(program, 0x00000000)) { // Loading at address 0 for simplicity
        status_msg = std::format("Success: Loaded {} instructions.", program.size());
        return true;
    } else {
        status_msg = "Error: Program does not fit in memory.";
        return false;
    }
}

int runApp() {
    // -------------------------------------------------------------------------
    // Core State
    // -------------------------------------------------------------------------
    Converter converter;
    mips::Cpu cpu;
    bool syncing = false;
    int tab_index = 0;
    std::vector<std::string> tab_entries = {
        " 🔢 Converter ",
        " 📊 CPU Dashboard ",
        " 📝 Program Loader "
    };

    // UI View State (Converter)
    std::string dec_str = "0";
    std::string hex_str = "0";
    std::string bin_str = "0";
    std::string grouped_bin_str = "0000";
    std::string mips_mnemonic_str = "nop";

    // UI View State (Loader)
    std::string filepath_str = "program.hex";
    std::string loader_status_str = "Ready.";

    // -------------------------------------------------------------------------
    // Converter Synchronization Logic
    // -------------------------------------------------------------------------
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

    // -------------------------------------------------------------------------
    // Components: Converter (Tab 0)
    // -------------------------------------------------------------------------
    InputOption dec_options; dec_options.on_change = [&] { update_views(Converter::Base::Decimal, dec_str); };
    Component dec_input = Input(&dec_str, "0", dec_options);

    InputOption hex_options; hex_options.on_change = [&] { update_views(Converter::Base::Hex, hex_str); };
    Component hex_input = Input(&hex_str, "0", hex_options);

    InputOption bin_options; bin_options.on_change = [&] { update_views(Converter::Base::Binary, bin_str); };
    Component bin_input = Input(&bin_str, "0", bin_options);

    Component converter_container = Container::Vertical({dec_input, hex_input, bin_input});

    // -------------------------------------------------------------------------
    // Components: CPU Controls (Tab 1)
    // -------------------------------------------------------------------------
    Component btn_step = Button(" Step (F10) ", [&] { cpu.step(); });
    Component btn_reset = Button(" Reset ", [&] { cpu.reset(); loader_status_str = "CPU Reset."; });
    Component controls_container = Container::Horizontal({btn_step, btn_reset});

    // -------------------------------------------------------------------------
    // Components: Loader (Tab 2)
    // -------------------------------------------------------------------------
    Component filepath_input = Input(&filepath_str, "path/to/program.hex");
    Component btn_load = Button(" Load File ", [&] {
        cpu.reset(); // Clear old state
        load_hex_file(filepath_str, cpu, loader_status_str);
    });
    Component loader_container = Container::Vertical({filepath_input, btn_load});

    // -------------------------------------------------------------------------
    // Main UI Routing & Layout
    // -------------------------------------------------------------------------
    MenuOption tab_options = MenuOption::HorizontalAnimated();
    // Optional: Add a custom transform to make the active tab stand out more cleanly
    tab_options.entries_option.transform = [](const EntryState& state) {
        Element e = text(state.label) | center;
        if (state.active) {
            e = e | bold | color(Color::Cyan); // Active tab text color
        }
        if (state.focused) {
            e = e | inverted; // Highlight when navigating with keyboard
        }
        return e | size(WIDTH, GREATER_THAN, 18); // Give tabs consistent breathing room
    };

    Component tab_selection = Menu(&tab_entries, &tab_index, tab_options);

    Component main_container = Container::Vertical({
        tab_selection,
        Container::Tab({
            converter_container,
            controls_container,
            loader_container
        }, &tab_index)
    });

    Component renderer = Renderer(main_container, [&] {

        Element current_view;

        if (tab_index == 0) {
            // --- TAB 0: CONVERTER ---
            current_view = vbox({
                hbox(text(" DEC: "), dec_input->Render() | flex) | border,
                hbox(text(" HEX: "), hex_input->Render() | flex) | border,
                hbox(text(" BIN: "), bin_input->Render() | flex) | border,
                separator(),
                vbox({
                    text(" Nibble View:") | dim,
                    text(" " + grouped_bin_str) | color(Color::Cyan),
                    text(""),
                    text(" Live MIPS Decode (32-bit):") | dim,
                    text(" " + mips_mnemonic_str) | color(Color::Yellow) | bold,
                }) | border,
            });
        }
        else if (tab_index == 1) {
            // --- TAB 1: CPU DASHBOARD ---

            // 1. Render Registers
            Elements col1, col2;
            for (int i = 0; i < 32; ++i) {
                auto reg_txt = text(std::format("${:<2} 0x{:08X}", i, cpu.regs().read(i)));
                if (i != 0 && i == cpu.regs().last_written()) {
                    reg_txt = reg_txt | bold | color(Color::Green);
                }
                if (i < 16) col1.push_back(reg_txt);
                else        col2.push_back(reg_txt);
            }
            auto reg_panel = window(text(" Registers "), hbox({
                vbox(col1) | flex, separatorEmpty(), vbox(col2) | flex
            }));

            // 2. Render Datapath / Execution State
            uint32_t pc = cpu.pc();
            std::string current_instr = "???";
            std::string raw_hex = "00000000";

            if (auto fetched = cpu.mem().read_word(pc)) {
                raw_hex = std::format("{:08X}", *fetched);
                if (auto decoded = mips::Decoder::decode(*fetched)) {
                    current_instr = std::string(mips::Decoder::mnemonic(*decoded));
                }
            }

            auto datapath_panel = window(text(" Datapath "), vbox({
                text(std::format("PC: 0x{:08X}", pc)) | bold | color(Color::Cyan),
                separatorLight(),
                text("Current Instruction:") | dim,
                text(std::format("{} (0x{})", current_instr, raw_hex)) | bold,
                separatorEmpty(),
                text("Last Control Signals:") | dim,
                text(std::format("RegWrite: {} | MemRead: {} | MemWrite: {}",
                     cpu.last_control().reg_write, cpu.last_control().mem_read, cpu.last_control().mem_write))
            }));

            // 3. Render Memory (Quick preview around PC)
            Elements mem_lines;
            for (uint32_t offset = 0; offset <= 16; offset += 4) {
                uint32_t addr = (pc >= 8 ? pc - 8 : 0) + offset; // Show a bit before and after PC
                if (auto val = cpu.mem().read_word(addr)) {
                    auto line = text(std::format("0x{:08X}: {:08X}", addr, *val));
                    if (addr == pc) line = line | bold | color(Color::Cyan); // Highlight active instruction
                    mem_lines.push_back(line);
                }
            }
            auto mem_panel = window(text(" Memory Preview "), vbox(mem_lines));

            current_view = vbox({
                hbox({
                    reg_panel | size(WIDTH, GREATER_THAN, 30),
                    vbox({ datapath_panel, mem_panel }) | flex
                }) | flex,
                window(text(" Controls "), controls_container->Render() | center)
            });
        }
        else if (tab_index == 2) {
            // --- TAB 2: LOADER ---
            current_view = window(text(" Program Loader (.hex) "), vbox({
                text("Enter path to a file containing hex machine code (one word per line)."),
                text("Lines starting with '#' are ignored.") | dim,
                separatorEmpty(),
                hbox(text(" File: "), filepath_input->Render() | flex) | border,
                separatorEmpty(),
                btn_load->Render() | size(WIDTH, LESS_THAN, 20),
                separatorEmpty(),
                text(" Status: " + loader_status_str) | bold
            }));
        }

        // --- ROOT LAYOUT ---
        return vbox({
            hbox({ text(" ⚙️ ClearCore MIPS Emulator ") | bold, filler(), tab_selection->Render() }),
            separator(),
            current_view | flex,
            separator(),
            text(" [Tab] Navigate • [Enter] Activate • [Esc/Ctrl+C] Quit") | dim | center
        }) | border | flex;
    });

    // -------------------------------------------------------------------------
    // Global Event Handler
    // -------------------------------------------------------------------------
    auto screen = ScreenInteractive::Fullscreen(); // Changed to Fullscreen for dashboard

    Component event_catcher = CatchEvent(renderer, [&](Event event) {
        if (event == Event::Escape || event == Event::Character(static_cast<char>('c' - 'a' + 1))) {
            screen.Exit();
            return true;
        }
        // Global hotkeys
        if (event == Event::F10 && tab_index == 1) {
            cpu.step();
            return true;
        }
        return false;
    });

    screen.Loop(event_catcher);
    return 0;
}

} // namespace nsc