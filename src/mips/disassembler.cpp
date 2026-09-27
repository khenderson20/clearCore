#include "mips/disassembler.h"
#include "mips/registers.h"

#include <cctype>
#include <format>
#include <string>

namespace mips {

namespace {

// Lowercase the mnemonic returned by the decoder ("ADD" → "add").
std::string lower_mnemonic(const DecodedInstr& dec) {
    std::string mn;
    for (const char c : Decoder::mnemonic(dec))
        mn += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return mn;
}

}  // namespace

std::string Disassembler::to_string(const DecodedInstr& dec, uint32_t pc) noexcept {
    try {
        const std::string mn = lower_mnemonic(dec);

        if (dec.format == InstrFormat::R) {
            const RFields&         r  = dec.r();
            const std::string_view rd = register_abi_name(r.rd);
            const std::string_view rs = register_abi_name(r.rs);
            const std::string_view rt = register_abi_name(r.rt);
            switch (r.funct) {
            case FunctCode::SLL:
            case FunctCode::SRL:
            case FunctCode::SRA:
                return std::format("{} ${}, ${}, {}", mn, rd, rt, r.shamt);
            case FunctCode::SLLV:
            case FunctCode::SRLV:
                return std::format("{} ${}, ${}, ${}", mn, rd, rt, rs);
            case FunctCode::JR:
                return std::format("{} ${}", mn, rs);
            case FunctCode::JALR:
                return std::format("{} ${}, ${}", mn, rd, rs);
            default:
                return std::format("{} ${}, ${}, ${}", mn, rd, rs, rt);
            }
        }

        if (dec.format == InstrFormat::I) {
            const IFields&         i    = dec.i();
            const std::string_view rs   = register_abi_name(i.rs);
            const std::string_view rt   = register_abi_name(i.rt);
            const int32_t          simm = Decoder::sign_extend(i.imm);
            switch (dec.opcode) {
            case Opcode::LW:
            case Opcode::LBU:
            case Opcode::LHU:
            case Opcode::SW:
                return std::format("{} ${}, {}(${})", mn, rt, simm, rs);
            case Opcode::LUI:
                return std::format("{} ${}, 0x{:04X}", mn, rt, i.imm);
            case Opcode::BEQ:
            case Opcode::BNE:
                return std::format("{} ${}, ${}, {:+d}", mn, rs, rt, simm);
            default:
                return std::format("{} ${}, ${}, {:+d}", mn, rt, rs, simm);
            }
        }

        if (dec.format == InstrFormat::J) {
            const uint32_t jaddr = ((pc + 4) & 0xF000'0000u) | (dec.j().target << 2);
            return std::format("{} 0x{:08X}", mn, jaddr);
        }

        return mn + " ???";
    } catch (...) {
        // std::format / variant access should never throw for a well-formed
        // DecodedInstr, but to_string is noexcept by contract — degrade to the
        // raw mnemonic rather than terminate.
        return "???";
    }
}

}  // namespace mips
