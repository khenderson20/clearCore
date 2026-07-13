#include "nsc/parse.h"

#include <string>

namespace nsc {

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
        int digit;
        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (c >= 'a' && c <= 'z') {
            digit = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'Z') {
            digit = c - 'A' + 10;
        } else {
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
