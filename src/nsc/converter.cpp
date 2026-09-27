#include "nsc/converter.h"

#include <string>

#include "nsc/format.h"
#include "nsc/parse.h"

namespace nsc {

bool Converter::update(const std::string& text, Base base) {
    if (const auto value = parse_base(text, static_cast<int>(base))) {
        value_ = *value;
        return true;
    }
    return false;
}

std::string Converter::as(const Base base) const {
    switch (base) {
    case Base::Binary:
        return to_binary(value_);
    case Base::Decimal:
        return to_decimal(value_);
    case Base::Hex:
        return to_hex(value_);
    default:
        return {};
    }
}

std::string Converter::bits() const {
    return group_bits(value_);
}

}  // namespace nsc
