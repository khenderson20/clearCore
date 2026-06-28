#include "nsc/ui.h"

#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <string>
#include <thread>

#include "nsc/converter.h"

#include "ftxui/component/component.hpp"
#include "ftxui/component/component_options.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"

using namespace ftxui;

namespace nsc {

int runApp() {

    // ---- Colors -------------------------------------------------------------
    // Cyan/teal palette — monochrome futuristic terminal aesthetic.

    auto cyan       = Color::RGB(0, 220, 210);
    auto cyan_dim   = Color::RGB(0, 130, 125);
    auto cyan_faint = Color::RGB(0, 60, 58);
    auto teal       = Color::RGB(0, 180, 170);
    auto slate      = Color::RGB(100, 116, 130);
    auto dark_bg    = Color::RGB(12, 18, 24);
    auto cell_bg    = Color::RGB(20, 35, 42);

    // ---- State --------------------------------------------------------------

    Converter converter;
    std::string bin, hex, dec;
    bool syncing = false;

    // ---- Animation state ----------------------------------------------------

    std::atomic<int> frame{0};
    std::atomic<bool> running{true};

    // ---- Sync logic ---------------------------------------------------------

    auto makeOnChange = [&](Converter::Base base, std::string* self) {
        return [&, base, self] {
            if (syncing) {
                return;
            }
            if (converter.update(*self, base)) {
                syncing = true;
                if (base != Converter::Base::Binary) {
                    bin = converter.as(Converter::Base::Binary);
                }
                if (base != Converter::Base::Hex) {
                    hex = converter.as(Converter::Base::Hex);
                }
                if (base != Converter::Base::Decimal) {
                    dec = converter.as(Converter::Base::Decimal);
                }
                syncing = false;
            }
        };
    };

    // ---- Input components ---------------------------------------------------

    InputOption binOpt, hexOpt, decOpt;

    binOpt.on_change = makeOnChange(Converter::Base::Binary, &bin);
    Component in_bin = Input(&bin, "binary", binOpt);

    hexOpt.on_change = makeOnChange(Converter::Base::Hex, &hex);
    Component in_hex = Input(&hex, "hex", hexOpt);

    decOpt.on_change = makeOnChange(Converter::Base::Decimal, &dec);
    Component in_dec = Input(&dec, "decimal", decOpt);

    // ---- Character filters --------------------------------------------------

    auto charFilter = [](auto isAllowed) {
        return CatchEvent([isAllowed](const Event& e) {
            if (!e.is_character()) {
                return false;
            }
            char c = e.character()[0];
            return !isAllowed(c);
        });
    };

    in_bin |= charFilter([](char c) { return c == '0' || c == '1'; });
    in_dec |= charFilter([](char c) {
        return std::isdigit(static_cast<unsigned char>(c)) != 0;
    });
    in_hex |= charFilter([](char c) {
        return std::isxdigit(static_cast<unsigned char>(c)) != 0;
    });

    // ---- Pulse title --------------------------------------------------------
    // The title breathes between dim and bright cyan using a sine wave tied to
    // the frame counter. Single color, no rainbow — just a slow glow pulse.

    auto pulseTitle = [&]() -> Element {
        const std::string title = "[ NUMBER SYSTEM CONVERTER ]";
        int f = frame.load();
        double wave = (std::sin(f * 0.12) + 1.0) / 2.0;  // 0.0 to 1.0
        auto r = static_cast<uint8_t>(0);
        auto g = static_cast<uint8_t>(130 + wave * 90);
        auto b = static_cast<uint8_t>(125 + wave * 85);
        return text(title) | bold | color(Color::RGB(r, g, b));
    };

    // ---- Scan line ----------------------------------------------------------
    // A thin bar of "━" characters with a bright dot that sweeps left to right,
    // like a scanning beam on a retro terminal. Everything stays in the
    // cyan/teal family.

    auto scanLine = [&](int width) -> Element {
        Elements cells;
        int f = frame.load();
        int pos = f % (width * 2);
        if (pos >= width) pos = (width * 2) - pos - 1;  // bounce

        for (int i = 0; i < width; ++i) {
            int dist = std::abs(i - pos);
            if (dist == 0) {
                cells.push_back(text("━") | bold | color(cyan));
            } else if (dist <= 3) {
                double fade = 1.0 - (dist / 4.0);
                auto g = static_cast<uint8_t>(60 + fade * 100);
                auto b = static_cast<uint8_t>(58 + fade * 95);
                cells.push_back(text("━") | color(Color::RGB(0, g, b)));
            } else {
                cells.push_back(text("━") | color(cyan_faint));
            }
        }
        return hbox(cells);
    };

    // ---- Bit grid -----------------------------------------------------------
    // 1-bits glow cyan on a dark cell; 0-bits are dim on a darker cell.

    auto bitGrid = [&]() -> Element {
        std::string bits = converter.bits();
        Elements cells;
        for (char ch : bits) {
            if (ch == ' ') {
                cells.push_back(text(" ") | size(WIDTH, EQUAL, 2));
            } else if (ch == '1') {
                cells.push_back(
                    text(" " + std::string(1, ch) + " ")
                    | bold
                    | color(cyan)
                    | bgcolor(cell_bg)
                );
            } else {
                cells.push_back(
                    text(" " + std::string(1, ch) + " ")
                    | color(slate)
                    | bgcolor(Color::RGB(15, 22, 28))
                );
            }
        }
        return hbox(cells);
    };

    // ---- Layout -------------------------------------------------------------

    auto container = Container::Vertical({in_bin, in_hex, in_dec});

    auto ui = Renderer(container, [&]() -> Element {
        auto row = [&](const std::string& label, const Component& input, Color c) {
            return hbox({
                text(label) | bold | color(c) | size(WIDTH, EQUAL, 10),
                input->Render() | flex | color(Color::RGB(220, 235, 240)),
            });
        };

        return vbox({
                   scanLine(44),
                   pulseTitle() | center,
                   separator() | color(cyan_faint),
                   row("  BIN", in_bin, teal),
                   row("  HEX", in_hex, cyan_dim),
                   row("  DEC", in_dec, teal),
                   separator() | color(cyan_faint),
                   hbox({text("  "), bitGrid()}) | center,
                   text("  Tab to move · Esc / Ctrl-C to quit") | color(slate) | center,
                   scanLine(44),
               }) |
               border | color(cyan_faint) | size(WIDTH, GREATER_THAN, 44);
    });

    // ---- Event loop ---------------------------------------------------------

    auto screen = ScreenInteractive::FitComponent();

    ui |= CatchEvent([&](const Event& e) {
        if (e == Event::Escape) {
            screen.ExitLoopClosure()();
            return true;
        }
        return false;
    });

    // ---- Animator thread ----------------------------------------------------

    std::thread animator([&] {
        while (running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(67));
            ++frame;
            screen.Post(Event::Custom);
        }
    });

    screen.Loop(ui);

    // ---- Cleanup ------------------------------------------------------------
    running = false;
    animator.join();
    return 0;
}

}  // namespace nsc