#ifndef NUMBER_SYSTEM_CONVERTER_FORMAT_H
#define NUMBER_SYSTEM_CONVERTER_FORMAT_H

#include <cstdint>
#include <string>

namespace nsc {

// Shortest binary string, no leading zeros ("0" for zero).
[[nodiscard]] std::string toBinary(std::uint64_t value);

// Uppercase hexadecimal, no prefix ("0" for zero).
[[nodiscard]] std::string toHex(std::uint64_t value);

// Decimal string.
[[nodiscard]] std::string toDecimal(std::uint64_t value);

// Binary digits left-padded to a multiple of four and grouped into nibbles,
// e.g. 0xAC -> "1010 1100".
[[nodiscard]] std::string groupBits(std::uint64_t value);

}  // namespace nsc

#endif  // NUMBER_SYSTEM_CONVERTER_FORMAT_H
