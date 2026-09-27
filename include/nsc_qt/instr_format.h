#pragma once

// ── instr_format.h ──────────────────────────────────────────────────────────
// "Raw word → assembly text" for the Qt Widgets panels. The text comes from the
// core mips::Disassembler, the one the TUI and the Qt Quick GUI already use, so
// every front end prints the same assembly (and the same immediates the
// assembler accepts: "ori $t0, $zero, 0xffff", never "-1").

#include "mips/decoder.h"
#include "mips/disassembler.h"

#include <cstdint>
#include <string>

namespace nsc::qt {

// Assembly text for an already-decoded instruction. `pc` is the instruction's
// own address; it resolves J/JAL targets.
[[nodiscard]] inline std::string format_decoded(const mips::DecodedInstr& d, uint32_t pc) {
    return mips::Disassembler::to_string(d, pc);
}

// As format_decoded, for a raw word; "(?/?)" when the word does not decode.
[[nodiscard]] inline std::string format_instr(uint32_t raw, uint32_t pc) {
    const auto decoded = mips::Decoder::decode(raw);
    return decoded ? format_decoded(*decoded, pc) : "(?/?)";
}

}  // namespace nsc::qt
