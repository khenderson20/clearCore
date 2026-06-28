#ifndef NUMBER_SYSTEM_CONVERTER_PARSE_H
#define NUMBER_SYSTEM_CONVERTER_PARSE_H

#include <cstdint>
#include <optional>
#include <string>

namespace nsc {

    // Parse `str`, interpreted in `base`, into a uint64_t.
    //
    // `base` is one of 2 (binary), 10 (decimal), or 16 (hexadecimal).
    // Returns std::nullopt when the input is empty, contains characters invalid for
    // the base, has trailing garbage, or overflows 64 bits.
    [[nodiscard]] std::optional<std::uint64_t> parseBase(const std::string& str, int base);

}  // namespace nsc


#endif //NUMBER_SYSTEM_CONVERTER_PARSE_H
