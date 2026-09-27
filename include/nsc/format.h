#pragma once

#include <cstdint>
#include <string>

namespace nsc {

// Shortest binary string, no leading zeros ("0" for zero).
[[nodiscard]] std::string to_binary(std::uint64_t value);

// Uppercase hexadecimal, no prefix ("0" for zero).
[[nodiscard]] std::string to_hex(std::uint64_t value);

// Decimal string.
[[nodiscard]] std::string to_decimal(std::uint64_t value);

// Binary digits left-padded to a multiple of four and grouped into nibbles,
// e.g. 0xAC -> "1010 1100".
[[nodiscard]] std::string group_bits(std::uint64_t value);

}  // namespace nsc
