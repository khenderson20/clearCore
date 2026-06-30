#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace mips {

    // ─── Register file ────────────────────────────────────────────────────────────
    // 32 general-purpose registers, each 32 bits wide (H&H §6.4).
    //
    // Register 0 ($zero) is hardwired to 0: reads always return 0 and writes are
    // silently discarded. This mirrors the hardware and lets the ISA use $zero as
    // both a constant-0 source and a discard sink (e.g. `SLL $zero,$zero,0` is NOP).
    class RegisterFile {
    public:
        static constexpr std::size_t kCount = 32;

        // Read register `idx`. Index 0 always reads as 0.
        [[nodiscard]] uint32_t read(uint8_t idx) const noexcept;

        // Write `value` to register `idx`. Writes to index 0 are ignored.
        void write(uint8_t idx, uint32_t value) noexcept;

        // Index of the most recently written register, or -1 if none since reset.
        // Exists for the Stage 2 TUI, which highlights the last-changed register.
        [[nodiscard]] int last_written() const noexcept { return last_written_; }

        // Restore all registers to 0 and clear the last-written marker.
        void reset() noexcept;

        // Const view for the TUI register panel / test inspection.
        [[nodiscard]] const std::array<uint32_t, kCount>& raw() const noexcept {
            return regs_;
        }

    private:
        std::array<uint32_t, kCount> regs_{};
        int last_written_ = -1;
    };

    // ─── ABI register names ───────────────────────────────────────────────────────
    // MIPS O32 ABI mnemonic for register `idx` (0–31), e.g. 8 → "t0", 31 → "ra".
    // Returns "??" for an out-of-range index. Lives here so the disassembler and
    // the TUI share one source of truth.
    [[nodiscard]] std::string_view register_abi_name(uint8_t idx) noexcept;

} // namespace mips