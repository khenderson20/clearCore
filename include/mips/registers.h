#pragma once

// ─── mips/registers.h ────────────────────────────────────────────────────────
// The 32×32 register file is ISA-agnostic and now lives in <isa/registers.h>.
// This header re-exports it as `mips::RegisterFile` for existing callers and
// adds the MIPS O32 ABI mnemonic table, which is MIPS-specific and stays here.

#include "isa/registers.h"

#include <string_view>

namespace mips {
using isa::RegisterFile;

// ─── ABI register names ───────────────────────────────────────────────────────
// MIPS O32 ABI mnemonic for register `idx` (0–31), e.g. 8 → "t0", 31 → "ra".
// Returns "??" for an out-of-range index. Lives here so the disassembler and
// the TUI share one source of truth. RISC-V provides its own table.
[[nodiscard]] std::string_view register_abi_name(uint8_t idx) noexcept;

}  // namespace mips
