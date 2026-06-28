#include "mips/decoder.h"

namespace mips {

// ─── Decoder::format_of ───────────────────────────────────────────────────────
InstrFormat Decoder::format_of(Opcode op) {
    switch (op) {
        case Opcode::SPECIAL:
            return InstrFormat::R;

        case Opcode::J:
        case Opcode::JAL:
            return InstrFormat::J;

        case Opcode::BEQ:
        case Opcode::BNE:
        case Opcode::ADDI:
        case Opcode::ADDIU:
        case Opcode::SLTI:
        case Opcode::SLTIU:
        case Opcode::ANDI:
        case Opcode::ORI:
        case Opcode::XORI:
        case Opcode::LUI:
        case Opcode::LW:
        case Opcode::LBU:
        case Opcode::LHU:
        case Opcode::SW:
            return InstrFormat::I;

        default:
            return InstrFormat::Unknown;
    }
}

// ─── Decoder::decode ──────────────────────────────────────────────────────────
std::optional<DecodedInstr> Decoder::decode(uint32_t instr) {
    const auto raw_op = static_cast<Opcode>(bits(instr, 31, 26));
    const InstrFormat fmt = format_of(raw_op);

    if (fmt == InstrFormat::Unknown) return std::nullopt;

    DecodedInstr result;
    result.raw    = instr;
    result.opcode = raw_op;
    result.format = fmt;

    switch (fmt) {
        case InstrFormat::R: {
            const auto funct = static_cast<FunctCode>(bits(instr, 5, 0));

            // Validate funct — reject reserved encodings rather than passing
            // garbage to the ALU. Unknown funct codes become a decode stall
            // once the pipeline is in place.
            switch (funct) {
                case FunctCode::SLL:
                case FunctCode::SRL:
                case FunctCode::SRA:
                case FunctCode::SLLV:
                case FunctCode::SRLV:
                case FunctCode::JR:
                case FunctCode::JALR:
                case FunctCode::ADD:
                case FunctCode::ADDU:
                case FunctCode::SUB:
                case FunctCode::SUBU:
                case FunctCode::AND:
                case FunctCode::OR:
                case FunctCode::XOR:
                case FunctCode::NOR:
                case FunctCode::SLT:
                case FunctCode::SLTU:
                    break;
                default:
                    return std::nullopt;
            }

            result.fields = RFields{
                .rs    = static_cast<uint8_t>(bits(instr, 25, 21)),
                .rt    = static_cast<uint8_t>(bits(instr, 20, 16)),
                .rd    = static_cast<uint8_t>(bits(instr, 15, 11)),
                .shamt = static_cast<uint8_t>(bits(instr, 10,  6)),
                .funct = funct,
            };
            break;
        }

        case InstrFormat::I: {
            result.fields = IFields{
                .rs  = static_cast<uint8_t> (bits(instr, 25, 21)),
                .rt  = static_cast<uint8_t> (bits(instr, 20, 16)),
                .imm = static_cast<uint16_t>(bits(instr, 15,  0)),
            };
            break;
        }

        case InstrFormat::J: {
            result.fields = JFields{
                .target = bits(instr, 25, 0),
            };
            break;
        }
        default: ;
    }

    return result;
}

// ─── Decoder::mnemonic ────────────────────────────────────────────────────────
std::string_view Decoder::mnemonic(const DecodedInstr& instr) {
    if (instr.format == InstrFormat::R) {
        switch (instr.r().funct) {
            case FunctCode::ADD:  return "ADD";
            case FunctCode::ADDU: return "ADDU";
            case FunctCode::SUB:  return "SUB";
            case FunctCode::SUBU: return "SUBU";
            case FunctCode::AND:  return "AND";
            case FunctCode::OR:   return "OR";
            case FunctCode::XOR:  return "XOR";
            case FunctCode::NOR:  return "NOR";
            case FunctCode::SLT:  return "SLT";
            case FunctCode::SLTU: return "SLTU";
            case FunctCode::SLL:  return "SLL";
            case FunctCode::SRL:  return "SRL";
            case FunctCode::SRA:  return "SRA";
            case FunctCode::SLLV: return "SLLV";
            case FunctCode::SRLV: return "SRLV";
            case FunctCode::JR:   return "JR";
            case FunctCode::JALR: return "JALR";
            default:              return "???";
        }
    }

    switch (instr.opcode) {
        case Opcode::J:     return "J";
        case Opcode::JAL:   return "JAL";
        case Opcode::BEQ:   return "BEQ";
        case Opcode::BNE:   return "BNE";
        case Opcode::ADDI:  return "ADDI";
        case Opcode::ADDIU: return "ADDIU";
        case Opcode::SLTI:  return "SLTI";
        case Opcode::SLTIU: return "SLTIU";
        case Opcode::ANDI:  return "ANDI";
        case Opcode::ORI:   return "ORI";
        case Opcode::XORI:  return "XORI";
        case Opcode::LUI:   return "LUI";
        case Opcode::LW:    return "LW";
        case Opcode::LBU:   return "LBU";
        case Opcode::LHU:   return "LHU";
        case Opcode::SW:    return "SW";
        default:            return "???";
    }
}

// ─── Decoder::sign_extend ─────────────────────────────────────────────────────
int32_t Decoder::sign_extend(uint16_t imm) {
    // Narrowing to int16_t reinterprets the bits as signed,
    // then widening to int32_t propagates the sign bit.
    return static_cast<int32_t>(static_cast<int16_t>(imm));
}

} // namespace mips