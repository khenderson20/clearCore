#include "mips/registers.h"

#include <gsl/gsl>

namespace mips {

uint32_t RegisterFile::read(uint8_t idx) const noexcept {
    // $zero is hardwired to 0. Mask keeps a malformed index in range.
    return (idx == 0) ? 0u : regs_[gsl::narrow_cast<std::size_t>(idx & 0x1Fu)];
}

void RegisterFile::write(uint8_t idx, uint32_t value) noexcept {
    if (idx == 0) return;  // discard writes to $zero
    regs_[gsl::narrow_cast<std::size_t>(idx & 0x1Fu)] = value;
    last_written_                                     = gsl::narrow_cast<int>(idx & 0x1Fu);
}

void RegisterFile::reset() noexcept {
    regs_.fill(0u);
    last_written_ = -1;
}

// ─── ABI register names ───────────────────────────────────────────────────────
std::string_view register_abi_name(uint8_t idx) noexcept {
    static constexpr std::array<std::string_view, 32> kNames = {
        "zero", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2",
        "t3",   "t4", "t5", "t6", "t7", "s0", "s1", "s2", "s3", "s4", "s5",
        "s6",   "s7", "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra",
    };
    return (idx < kNames.size()) ? kNames[idx] : std::string_view{"??"};
}
}  // namespace mips
