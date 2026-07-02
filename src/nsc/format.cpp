#include "nsc/format.h"

#include <format>
#include <string>

namespace nsc {

std::string toBinary(std::uint64_t value) {
    // Special case: zero
    if (value == 0) {
        return "0";
    }

    std::string out;
    while (value > 0) {
        // Extract the least-significant bit and append ('0' or '1').
        out.push_back(static_cast<char>('0' + (value & 1u)));
        // Shift right to process the next bit.
        value >>= 1;
    }

    // We built the string backwards, so reverse it.
    return {out.rbegin(), out.rend()};
}

std::string toHex(std::uint64_t value) {
    return std::format("{:X}", value);
}

std::string toDecimal(std::uint64_t value) {
    return std::to_string(value);
}

std::string groupBits(std::uint64_t value) {
    std::string bits = toBinary(value);

    // Left pad so the length is a multiple of 4.
    while (bits.size() % 4 != 0) {
        bits.insert(bits.begin(), '0');
    }

    // Insert a space every 4 characters.
    std::string out;
    for (size_t i = 0; i < bits.size(); ++i) {
        if (i > 0 && (i % 4) == 0) {
            out.push_back(' ');
        }
        out.push_back(bits[i]);
    }
    return {out};
}

}  // namespace nsc