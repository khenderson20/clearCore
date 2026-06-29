#include "mips/registers.h"

namespace mips {

uint32_t RegisterFile::read(uint8_t idx) const noexcept {
    // $zero is hardwired to 0. Mask keeps a malformed index in range.
    return (idx == 0) ? 0u : regs_[idx & 0x1Fu];
}

void RegisterFile::write(uint8_t idx, uint32_t value) noexcept {
    if (idx == 0) return;               // discard writes to $zero
    regs_[idx & 0x1Fu] = value;
    last_written_ = idx & 0x1F;
}

void RegisterFile::reset() noexcept {
    regs_.fill(0u);
    last_written_ = -1;
}

} // namespace mips
