#pragma once

// ─── disassembler.h ──────────────────────────────────────────────────────────
// Turns a DecodedInstr back into canonical lowercase MIPS assembly, e.g.
//   "add $t2, $t0, $t1", "lw $t0, -4($sp)", "j 0x00400000".
//
// Pure logic, zero UI dependency — moved out of the FTXUI layer so it can be
// unit-tested directly and reused by the execution-trace panel, the decode
// panel, and (eventually) the Stage 3 assembler's round-trip tests.

#include "mips/decoder.h"

#include <cstdint>
#include <string>

namespace mips {

class Disassembler {
public:
    // Reconstruct assembly text for `dec`. `pc` is the address of the
    // instruction itself; it is only needed to resolve J/JAL absolute targets
    // (target field × 4, combined with PC+4[31:28]).
    [[nodiscard]] static std::string to_string(const DecodedInstr& dec, uint32_t pc) noexcept;
};

}  // namespace mips
