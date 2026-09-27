#pragma once

#include <array>
#include <cstdint>

// ─── isa/registers.h ─────────────────────────────────────────────────────────
// ISA-agnostic general-purpose register file. Both MIPS and RV32I have exactly
// 32 GPRs with register 0 hardwired to zero, so this type is shared verbatim.
// ABI mnemonic tables (which differ per ISA) live with each backend, not here.

namespace isa {

// ─── Register file ────────────────────────────────────────────────────────────
// 32 general-purpose registers, each 32 bits wide.
//
// Register 0 is hardwired to 0: reads always return 0 and writes are silently
// discarded. This mirrors the hardware and lets the ISA use it as both a
// constant-0 source and a discard sink.
class RegisterFile {
public:
    static constexpr std::size_t kCount = 32;

    // Read register `idx`. Index 0 always reads as 0.
    [[nodiscard]] uint32_t read(uint8_t idx) const noexcept;

    // Write `value` to register `idx`. Writes to index 0 are ignored.
    void write(uint8_t idx, uint32_t value) noexcept;

    // Index of the most recently written register, or -1 if none since reset.
    // Exists for the TUI, which highlights the last-changed register.
    [[nodiscard]] int last_written() const noexcept { return last_written_; }

    // Restore all registers to 0 and clear the last-written marker.
    void reset() noexcept;

    // Const view for the TUI register panel / test inspection.
    [[nodiscard]] const std::array<uint32_t, kCount>& raw() const noexcept { return regs_; }

private:
    std::array<uint32_t, kCount> regs_{};
    int                          last_written_ = -1;
};

}  // namespace isa
