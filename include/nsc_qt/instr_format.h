#pragma once

// ── instr_format.h ──────────────────────────────────────────────────────────
// Shared "raw word → human-readable assembly" formatter for the datapath
// visualizers. Moved out of datapath_widget.cpp so the schematic datapath
// widget can reuse it without duplicating the format switch.

#include "mips/decoder.h"
#include "mips/registers.h"

#include <cstdint>
#include <sstream>
#include <string>

namespace nsc::qt {

// Format a decoded instruction as a human-readable string. This is assembly
// notation (mnemonics, register names), not natural-language UI copy, so it
// intentionally is not routed through tr().
inline std::string format_instr(uint32_t raw) {
    using namespace mips;
    auto decoded = Decoder::decode(raw);
    if (!decoded) return "(?/?)";
    const auto&        d  = *decoded;
    std::string        mn = std::string(Decoder::mnemonic(d));
    std::ostringstream os;
    os << mn << " ";
    switch (d.format) {
    case InstrFormat::R: {
        const auto& r = d.r();
        if (d.opcode == Opcode::SPECIAL) {
            using F = FunctCode;
            if (r.funct == F::SLL || r.funct == F::SRL || r.funct == F::SRA) {
                os << "$" << register_abi_name(r.rd) << ", $" << register_abi_name(r.rt) << ", "
                   << +r.shamt;
            } else if (r.funct == F::JR) {
                os << "$" << register_abi_name(r.rs);
            } else if (r.funct == F::JALR) {
                os << "$" << register_abi_name(r.rd) << ", $" << register_abi_name(r.rs);
            } else {
                os << "$" << register_abi_name(r.rd) << ", $" << register_abi_name(r.rs) << ", $"
                   << register_abi_name(r.rt);
            }
        }
        break;
    }
    case InstrFormat::I: {
        const auto& i = d.i();
        if (d.opcode == Opcode::LW || d.opcode == Opcode::LBU || d.opcode == Opcode::LHU ||
            d.opcode == Opcode::SW) {
            os << "$" << register_abi_name(i.rt) << ", " << static_cast<int16_t>(i.imm) << "($"
               << register_abi_name(i.rs) << ")";
        } else if (d.opcode == Opcode::LUI) {
            os << "$" << register_abi_name(i.rt) << ", 0x" << std::hex << i.imm;
        } else if (d.opcode == Opcode::BEQ || d.opcode == Opcode::BNE) {
            os << "$" << register_abi_name(i.rs) << ", $" << register_abi_name(i.rt) << ", "
               << static_cast<int16_t>(i.imm);
        } else {
            os << "$" << register_abi_name(i.rt) << ", $" << register_abi_name(i.rs) << ", "
               << static_cast<int16_t>(i.imm);
        }
        break;
    }
    case InstrFormat::J: {
        const auto& j = d.j();
        os << "0x" << std::hex << j.target;
        break;
    }
    default:
        break;
    }
    return os.str();
}

}  // namespace nsc::qt
