#include "mips/memory.h"

#include <algorithm>
#include <gsl/gsl>
#include <vector>

namespace mips {

Memory::Memory(std::size_t size_bytes) : data_(size_bytes, 0u) {}

bool Memory::in_bounds(uint32_t addr, std::size_t n) const noexcept {
    // 64-bit math so addr + n cannot wrap around at the top of the range.
    return static_cast<std::uint64_t>(addr) + n <= data_.size();
}

std::optional<uint32_t> Memory::read_word(uint32_t addr) const noexcept {
    if ((addr & 0x3u) != 0 || !in_bounds(addr, 4)) return std::nullopt;
    return static_cast<uint32_t>(data_[addr]) | (static_cast<uint32_t>(data_[addr + 1]) << 8) |
           (static_cast<uint32_t>(data_[addr + 2]) << 16) |
           (static_cast<uint32_t>(data_[addr + 3]) << 24);
}

std::optional<uint16_t> Memory::read_half(uint32_t addr) const noexcept {
    if ((addr & 0x1u) != 0 || !in_bounds(addr, 2)) return std::nullopt;
    return static_cast<uint16_t>(data_[addr] | (static_cast<uint16_t>(data_[addr + 1]) << 8));
}

std::optional<uint8_t> Memory::read_byte(uint32_t addr) const noexcept {
    if (!in_bounds(addr, 1)) return std::nullopt;
    return data_[addr];
}

bool Memory::write_word(uint32_t addr, uint32_t value) noexcept {
    if ((addr & 0x3u) != 0 || !in_bounds(addr, 4)) return false;
    data_[addr]     = gsl::narrow_cast<uint8_t>(value & 0xFFu);
    data_[addr + 1] = gsl::narrow_cast<uint8_t>((value >> 8) & 0xFFu);
    data_[addr + 2] = gsl::narrow_cast<uint8_t>((value >> 16) & 0xFFu);
    data_[addr + 3] = gsl::narrow_cast<uint8_t>((value >> 24) & 0xFFu);
    return true;
}

bool Memory::write_half(uint32_t addr, uint16_t value) noexcept {
    if ((addr & 0x1u) != 0 || !in_bounds(addr, 2)) return false;
    data_[addr]     = gsl::narrow_cast<uint8_t>(value & 0xFFu);
    data_[addr + 1] = gsl::narrow_cast<uint8_t>((value >> 8) & 0xFFu);
    return true;
}

bool Memory::write_byte(uint32_t addr, uint8_t value) noexcept {
    if (!in_bounds(addr, 1)) return false;
    data_[addr] = value;
    return true;
}

bool Memory::load_words(uint32_t addr, const std::vector<uint32_t>& words) noexcept {
    // Word alignment is a precondition, not a runtime-recoverable error: a
    // misaligned base would make every write_word below silently fail while we
    // still returned success, loading nothing. Program/data images always load
    // at word-aligned bases, so a violation is a caller bug — fail loudly.
    Expects((addr & 0x3u) == 0);
    if (!in_bounds(addr, words.size() * 4)) return false;
    for (std::size_t i = 0; i < words.size(); ++i) {
        write_word(addr + gsl::narrow_cast<uint32_t>(i * 4), words[i]);
    }
    return true;
}

void Memory::reset() noexcept {
    std::ranges::fill(data_, uint8_t{0});
}

}  // namespace mips
