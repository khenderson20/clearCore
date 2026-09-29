#include "mips/program_loader.h"

#include <cctype>
#include <charconv>
#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

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

        // Optional "0x"/"0X" prefix — std::from_chars does not accept one.
        std::string_view tok{clean};
        if (tok.size() > 2 && tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X'))
            tok.remove_prefix(2);

        // from_chars into uint32_t rather than std::stoul: `unsigned long` is
        // 64-bit on LP64 but 32-bit on Windows (LLP64), so stoul accepted an
        // over-wide token on Linux (silently truncating via the cast) while
        // throwing out_of_range on Windows — the same file parsed differently
        // per platform. from_chars is width-explicit and rejects overflow
        // identically everywhere. It also reports failure by return value,
        // which is the core's error convention.
        uint32_t   v     = 0;
        const auto first = tok.data();
        const auto last  = first + tok.size();

        if (const auto [ptr, ec] = std::from_chars(first, last, v, 16);
            ec != std::errc{} || ptr != last) {
            out.words.clear();
            out.error = std::format("Bad hex on line {}", n);
            return out;
        }
        out.words.push_back(v);
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
