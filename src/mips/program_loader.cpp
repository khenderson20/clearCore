#include "mips/program_loader.h"

#include <cctype>
#include <format>
#include <fstream>
#include <string>

namespace mips {

HexProgram parse_hex_program(std::istream& in) {
    HexProgram  out;
    std::string line;
    int         n = 0;

    while (std::getline(in, line)) {
        ++n;

        // Strip trailing comment.
        if (const auto cp = line.find('#'); cp != std::string::npos) line.erase(cp);

        // Strip all whitespace.
        std::string clean;
        clean.reserve(line.size());
        for (const char c : line)
            if (!std::isspace(static_cast<unsigned char>(c))) clean += c;

        if (clean.empty()) continue;

        try {
            std::size_t         used = 0;
            const unsigned long v    = std::stoul(clean, &used, 16);
            if (used != clean.size()) {
                out.words.clear();
                out.error = std::format("Bad hex on line {}", n);
                return out;
            }
            out.words.push_back(static_cast<uint32_t>(v));
        } catch (...) {
            out.words.clear();
            out.error = std::format("Bad hex on line {}", n);
            return out;
        }
    }
    return out;  // error == nullopt ⇒ success (an empty program is valid)
}

HexProgram load_hex_file(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        HexProgram out;
        out.error = std::format("Cannot open '{}'", path);
        return out;
    }
    return parse_hex_program(f);
}

}  // namespace mips
