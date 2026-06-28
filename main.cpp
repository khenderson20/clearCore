#include <cctype>
#include <cstdint>
#include <string>

#include "convert.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/component_options.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"

using namespace ftxui;

int main() {
    // Shared source of truth and the three editable buffers.
    uint64_t value = 0;
    std::string bin, hex, dec;
    bool syncing = false;  // re-entrancy guard for on_change cascades

    // Identify each field so resync() can skip the one being edited.
    enum Field { kBin = 2, kDec = 10, kHex = 16 };

    auto resync = [&](int except) {
        if (except != kBin) bin = toBinary(value);
        if (except != kDec) dec = toDecimal(value);
        if (except != kHex) hex = toHex(value);
    };

    // Build the on_change handler for a field: parse it, and on success update
    // every other field from the shared value.
    auto makeSync = [&](int base, const std::string* self) {
        return [&, base, self] {
            if (syncing) {
                return;
            }
            if (auto v = parseBase(*self, base)) {
                value = *v;
                syncing = true;
                resync(base);
                syncing = false;
            }
        };
    };

    InputOption binOpt;
    binOpt.on_change = makeSync(kBin, &bin);
    InputOption decOpt;
    decOpt.on_change = makeSync(kDec, &dec);
    InputOption hexOpt;
    hexOpt.on_change = makeSync(kHex, &hex);

    Component in_bin = Input(&bin, "binary", binOpt);
    Component in_dec = Input(&dec, "decimal", decOpt);
    Component in_hex = Input(&hex, "hex", hexOpt);

    // Reject characters outside each base's alphabet. Returning true consumes
    // the event; navigation/control keys (non-character events) pass through.
    auto filter = [](auto allowed) {
        return CatchEvent([allowed](const Event& e) {
            if (!e.is_character()) {
                return false;
            }
            char c = e.character()[0];
            return !allowed(c);
        });
    };

    in_bin |= filter([](char c) { return c == '0' || c == '1'; });
    in_dec |= filter([](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
    in_hex |= filter([](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; });

    auto container = Container::Vertical({in_bin, in_hex, in_dec});

    auto ui = Renderer(container, [&] {
        auto row = [](const std::string& label, const Component& input) {
            return hbox({
                text(label) | bold | size(WIDTH, EQUAL, 10),
                input->Render() | flex,
            });
        };
        return vbox({
                   text("Number System Converter") | bold | center,
                   separator(),
                   row("  BIN", in_bin),
                   row("  HEX", in_hex),
                   row("  DEC", in_dec),
                   separator(),
                   hbox({text("  bits  ") | dim, text(groupBits(value)) | dim}),
                   text("  Tab to move · Esc / Ctrl-C to quit") | dim | center,
               }) |
               border | size(WIDTH, GREATER_THAN, 44);
    });

    auto screen = ScreenInteractive::FitComponent();

    // Quit on Escape (Ctrl-C already exits by default).
    ui |= CatchEvent([&](const Event& e) {
        if (e == Event::Escape) {
            screen.ExitLoopClosure()();
            return true;
        }
        return false;
    });

    screen.Loop(ui);
    return 0;
}
