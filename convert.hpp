#ifndef CONVERT_HPP
#define CONVERT_HPP

#include <cstdint>
#include <format>
#include <optional>
#include <string>

// Pure conversion logic — no UI dependencies, easy to unit-test.

// Parse a string in the given base into a uint64_t. Returns nullopt on empty,
// invalid, or out-of-range input. base is 2 (binary), 10 (decimal), or 16 (hex).
inline std::optional<uint64_t> parseBase(const std::string& s, int base) {
    if (s.empty()) {
        return std::nullopt;
    }
    try {
        size_t consumed = 0;
        uint64_t v = std::stoull(s, &consumed, base);
        // Reject trailing garbage that stoull silently stops at.
        if (consumed != s.size()) {
            return std::nullopt;
        }
        return v;
    } catch (...) {
        return std::nullopt;
    }
}

// Shortest binary string, no leading zeros ("0" for zero).
inline std::string toBinary(uint64_t v) {
    if (v == 0) {
        return "0";
    }
    std::string out;
    while (v > 0) {
        out.push_back(static_cast<char>('0' + (v & 1u)));
        v >>= 1;
    }
    return std::string(out.rbegin(), out.rend());
}

inline std::string toHex(uint64_t v) {
    return std::format("{:X}", v);
}

inline std::string toDecimal(uint64_t v) {
    return std::to_string(v);
}

// Binary digits grouped into nibbles for readability, e.g. "1010 1100".
inline std::string groupBits(uint64_t v) {
    std::string bits = toBinary(v);
    // Left - pad so the length is a multiple of 4.
    while (bits.size() % 4 != 0) {
        bits.insert(bits.begin(), '0');
    }
    std::string out;
    for (size_t i = 0; i < bits.size(); ++i) {
        if (i > 0 && i % 4 == 0) {
            out.push_back(' ');
        }
        out.push_back(bits[i]);
    }
    return out;
}

#endif  // CONVERT_HPP
