#include "mips/data_memory.h"

namespace mips {

std::optional<uint32_t> load_data(const isa::Memory& mem, Opcode op, uint32_t addr) noexcept {
    switch (op) {
    case Opcode::LW:
        return mem.read_word(addr);
    case Opcode::LBU:
        if (const auto v = mem.read_byte(addr)) return *v;
        return std::nullopt;
    case Opcode::LHU:
        if (const auto v = mem.read_half(addr)) return *v;
        return std::nullopt;
    default:
        return std::nullopt;
    }
}

bool store_data(isa::Memory& mem, Opcode op, uint32_t addr, uint32_t value) noexcept {
    switch (op) {
    case Opcode::SW:
        return mem.write_word(addr, value);
    default:
        return false;
    }
}

}  // namespace mips
