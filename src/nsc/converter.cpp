#include "nsc/converter.h"

#include <string>

#include "nsc/format.h"
#include "nsc/parse.h"

namespace nsc {

bool Converter::update(const std::string& text, Base base) {
    if (const auto value = parseBase(text, static_cast<int>(base))) {
        value_ = *value;
        return true;
    }
    return false;
}

std::string Converter::as(const Base base) const {
    switch (base) {
    case Base::Binary:
        return toBinary(value_);
    case Base::Decimal:
        return toDecimal(value_);
    case Base::Hex:
        return toHex(value_);
    default:
        return {};
    }
}

std::string Converter::bits() const {
    return groupBits(value_);
}

}  // namespace nsc
