#pragma once

// ─── program_loader.h ────────────────────────────────────────────────────────
// Parses a hex program listing into a flat std::vector<uint32_t> suitable for
// IProcessor::load_program. Pure logic, no UI: the stream overload is unit-
// testable with a std::istringstream, and the file overload is a thin wrapper.
//
// Format: one 32-bit word per line, in hex (with or without "0x"). Anything
// from '#' to end-of-line is a comment. Blank lines are ignored. Whitespace
// within a line is stripped before parsing.

#include <cstdint>
#include <istream>
#include <optional>
#include <string>
#include <vector>

namespace mips {

// Result of a parse. On success, `error` is empty and `words` holds the program.
// On failure, `error` carries a human-readable message and `words` is empty.
struct HexProgram {
    std::vector<uint32_t>      words;
    std::optional<std::string> error;  // engaged ⇔ parse failed

    [[nodiscard]] bool ok() const noexcept { return !error.has_value(); }
    explicit           operator bool() const noexcept { return ok(); }
};

// Parse a hex listing from an arbitrary input stream.
[[nodiscard]] HexProgram parse_hex_program(std::istream& in);

// Open `path` and parse it. Reports a "cannot open" error if the file is
// missing or unreadable.
[[nodiscard]] HexProgram load_hex_file(const std::string& path);

}  // namespace mips
