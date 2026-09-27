#pragma once

// ─── mips/registers.h ────────────────────────────────────────────────────────
// The MIPS O32 ABI mnemonic table. The register file itself is ISA-agnostic:
// use isa::RegisterFile from <isa/registers.h>. There is deliberately no
// mips::RegisterFile alias, so a front end cannot name the ISA-agnostic type
// through a MIPS header (#237).

#include <cstdint>
#include <string_view>

namespace mips {

// ─── ABI register names ───────────────────────────────────────────────────────
// MIPS O32 ABI mnemonic for register `idx` (0–31), e.g. 8 → "t0", 31 → "ra".
// Returns "??" for an out-of-range index. Lives here so the disassembler and
// the TUI share one source of truth. RISC-V provides its own table.
[[nodiscard]] std::string_view register_abi_name(uint8_t idx) noexcept;

}  // namespace mips
