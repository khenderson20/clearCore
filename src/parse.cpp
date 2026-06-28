#include "nsc/parse.h"

namespace nsc {

    std::optional<std::uint64_t> parseBase(const std::string& str, int base) {
        // Empty strings are invalid
        if (str.empty()) {
            return std::nullopt;
        }

        try {
            // Parse str in the given base.
            // stoull return the value and sets 'consumed' to the number of characters it actually parsed.
            size_t consumed = 0;
            std::uint64_t value = std::stoull(str, &consumed, base);

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