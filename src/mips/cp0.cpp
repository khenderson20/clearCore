#include "mips/cp0.h"

namespace mips {

std::string_view exception_name(ExceptionCode code) noexcept {
    switch (code) {
    case ExceptionCode::Int:
        return "Int";
    case ExceptionCode::AdEL:
        return "AdEL";
    case ExceptionCode::AdES:
        return "AdES";
    case ExceptionCode::Sys:
        return "Sys";
    case ExceptionCode::Bp:
        return "Bp";
    case ExceptionCode::RI:
        return "RI";
    case ExceptionCode::Ov:
        return "Ov";
    default:
        return "??";
    }
}

uint32_t Cp0::raise(ExceptionCode code, uint32_t faulting_pc, uint32_t bad_addr) noexcept {
    last_exc_ = code;
    epc_      = faulting_pc;
    cause_    = (cause_ & ~0x7Cu) | (static_cast<uint32_t>(code) << 2);
    status_ |= kStatusEXL;
    bad_vaddr_ = bad_addr;
    return kExceptionVector;
}

uint32_t Cp0::eret() noexcept {
    status_ &= ~kStatusEXL;
    return epc_;
}

uint32_t Cp0::read(uint8_t reg) const noexcept {
    switch (reg) {
    case kRegBadVAddr:
        return bad_vaddr_;
    case kRegStatus:
        return status_;
    case kRegCause:
        return cause_;
    case kRegEpc:
        return epc_;
    default:
        return 0;
    }
}

void Cp0::write(uint8_t reg, uint32_t value) noexcept {
    switch (reg) {
    case kRegStatus:
        status_ = value;
        break;
    case kRegCause:
        cause_ = value;
        break;
    case kRegEpc:
        epc_ = value;
        break;
    default:
        break;
    }
}

void Cp0::reset() noexcept {
    status_    = 0;
    cause_     = 0;
    epc_       = 0;
    bad_vaddr_ = 0;
}

}  // namespace mips
