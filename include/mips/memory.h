#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace mips {

// ─── Memory ───────────────────────────────────────────────────────────────────
// Flat, byte-addressable RAM with word/half/byte access helpers.
//
// Endianness: little-endian. MIPS hardware can be configured either way; this
// emulator stores the least-significant byte at the lowest address, which keeps
// the host (x86/ARM) and emulated views identical and simplifies hex-dump
// debugging in the Stage 2 memory panel.
//
// All accesses are bounds-checked and return std::optional (reads) or a success
// bool (writes) rather than trapping, matching the project's "optional for
// fallible operations" convention. Word and half accesses must be naturally
// aligned (4- and 2-byte respectively); a misaligned access fails like an OOB
// one, modelling a MIPS address-error exception without the exception machinery.
class Memory {
public:
    explicit Memory(std::size_t size_bytes);

    [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }

    // Reads — std::nullopt on out-of-bounds or misalignment.
    [[nodiscard]] std::optional<uint32_t> read_word(uint32_t addr) const noexcept;
    [[nodiscard]] std::optional<uint16_t> read_half(uint32_t addr) const noexcept;
    [[nodiscard]] std::optional<uint8_t>  read_byte(uint32_t addr) const noexcept;

    // Writes — false on out-of-bounds or misalignment; memory left unchanged.
    bool write_word(uint32_t addr, uint32_t value) noexcept;
    bool write_half(uint32_t addr, uint16_t value) noexcept;
    bool write_byte(uint32_t addr, uint8_t  value) noexcept;

    // Bulk-load a blob of 32-bit words starting at `addr` (e.g. a program
    // image). Returns false if it would not fit. Stored little-endian.
    bool load_words(uint32_t addr, const std::vector<uint32_t>& words) noexcept;

    // Zero the entire address space.
    void reset() noexcept;

    [[nodiscard]] const std::vector<uint8_t>& raw() const noexcept { return data_; }

private:
    [[nodiscard]] bool in_bounds(uint32_t addr, std::size_t n) const noexcept;

    std::vector<uint8_t> data_;
};

} // namespace mips
