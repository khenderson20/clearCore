//
// Created by venus on 6/28/26.
//

#ifndef NUMBER_SYSTEM_CONVERTER_CONVERTER_H
#define NUMBER_SYSTEM_CONVERTER_CONVERTER_H

#include <cstdint>
#include <string>

namespace nsc {

    // Holds a single uint64_t "source of truth" and exposes it as binary, decimal,
    // or hexadecimal views. Pure logic, no UI dependencies — unit-test friendly.
    //
    // This is the piece that used to live as loose lambdas inside main(): the
    // `value`, the parse-on-edit, and the re-format. The UI layer owns only the
    // editable string buffers and drives this class.
    class Converter {
    public:
        enum class Base : int {
            Binary = 2,
            Decimal = 10,
            Hex = 16,
        };

        // Parse `text` in `base` and adopt the result as the current value.
        // Returns true on success; on failure the stored value is left unchanged.
        bool update(const std::string& text, Base base);

        // The current value.
        [[nodiscard]] std::uint64_t value() const noexcept { return value_; }

        // The current value formatted in `base` (no grouping, no prefix).
        [[nodiscard]] std::string as(Base base) const;

        // The current value as nibble-grouped binary, e.g. "1010 1100".
        [[nodiscard]] std::string bits() const;

    private:
        std::uint64_t value_ = 0;
    };

}  // namespace nsc
#endif //NUMBER_SYSTEM_CONVERTER_CONVERTER_H
