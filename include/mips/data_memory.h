#pragma once

// ─── data_memory.h ───────────────────────────────────────────────────────────
// Load and store access shared by both CPU models, so the set of access widths
// is defined in one place: adding LB/LH/SB/SH means editing only this file.

#include "isa/memory.h"
#include "mips/decoder.h"

#include <cstdint>
#include <optional>

namespace mips {

// The value `op` loads from `addr` (LBU and LHU zero-extend), or nullopt when
// the access is out of range or misaligned — an AdEL — or `op` is not a load.
[[nodiscard]] std::optional<uint32_t> load_data(const isa::Memory& mem, Opcode op,
                                                uint32_t addr) noexcept;

// Performs the store `op` of `value` at `addr`. False when the access is out of
// range or misaligned — an AdES — or `op` is not a store.
[[nodiscard]] bool store_data(isa::Memory& mem, Opcode op, uint32_t addr, uint32_t value) noexcept;

}  // namespace mips
