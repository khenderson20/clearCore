#include "nsc/parse.h"

#include <string>

namespace nsc {

namespace {

// Value of an alphanumeric digit character: '0'-'9' → 0-9, 'a'-'z' and 'A'-'Z'
// → 10-35. Anything else is -1.
constexpr int digit_value(unsigned char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return -1;
}

}  // anonymous namespace

std::optional<std::uint64_t> parseBase(const std::string& str, int base) {
    // Empty strings are invalid
    if (str.empty()) {
        return std::nullopt;
    }

    // std::stoull is lenient in ways the contract forbids: it silently accepts a
    // leading '+'/'-' (wrapping negatives into the unsigned range), surrounding
    // whitespace, and base-16 "0x" prefixes. Reject anything that is not a plain
    // digit for `base` up front so those inputs return nullopt as documented.
    for (const unsigned char c : str) {
        const int digit = digit_value(c);
        if (digit < 0) {
            return std::nullopt;  // sign, whitespace, '0x' prefix, punctuation, …
        }
        if (digit >= base) {
            return std::nullopt;  // valid character, but not for this base
        }
    }

    try {
        // Parse str in the given base.
        // stoull return the value and sets 'consumed' to the number of characters it actually
        // parsed.
        size_t        consumed = 0;
        std::uint64_t value    = std::stoull(str, &consumed, base);

        // Reject partial parses (trailing garbage that stoull ignored).
        // e.g., "12x" in base 10 -> consumed = 2, str.size() = 3 -> FAIL
        if (consumed != str.size()) {
            return std::nullopt;
        }

        return value;
    } catch (...) {
        // stoull throws on invalid format or overflow
        return std::nullopt;
    }
}

}  // namespace nsc
