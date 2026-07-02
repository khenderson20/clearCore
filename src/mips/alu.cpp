#include "mips/alu.h"

namespace mips {

// ─── Alu::control ─────────────────────────────────────────────────────────────
// The ALU control block (H&H Figure 7.10).
// Takes the decoded instruction and produces the AluOp the execute() needs.
//
// A few non-obvious mappings worth noting:
//   SLLV/SRLV → SLL/SRL  : same operation; the CPU provides rs[4:0] as shamt
//   JR/JALR   → PASS_A   : ALU passes rs through so CPU can jump to it
//   LW/SW/Lx  → ADDU     : address calculation is base + sign_extend(offset)
//   BEQ/BNE   → SUBU     : CPU checks the zero flag to decide branch taken
std::optional<AluOp> Alu::control(const DecodedInstr& instr) {
    switch (instr.format) {

        case InstrFormat::R:
            switch (instr.r().funct) {
                case FunctCode::ADD:  return AluOp::ADD;
                case FunctCode::ADDU: return AluOp::ADDU;
                case FunctCode::SUB:  return AluOp::SUB;
                case FunctCode::SUBU: return AluOp::SUBU;
                case FunctCode::AND:  return AluOp::AND;
                case FunctCode::OR:   return AluOp::OR;
                case FunctCode::XOR:  return AluOp::XOR;
                case FunctCode::NOR:  return AluOp::NOR;
                case FunctCode::SLT:  return AluOp::SLT;
                case FunctCode::SLTU: return AluOp::SLTU;
                case FunctCode::SLL:
                case FunctCode::SLLV: return AluOp::SLL;
                case FunctCode::SRL:
                case FunctCode::SRLV: return AluOp::SRL;
                case FunctCode::SRA:  return AluOp::SRA;
                case FunctCode::JR:
                case FunctCode::JALR: return AluOp::PASS_A;
                default:              return std::nullopt;
            }

        case InstrFormat::I:
            switch (instr.opcode) {
                case Opcode::ADDI:  return AluOp::ADD;
                case Opcode::ADDIU: return AluOp::ADDU;
                case Opcode::SLTI:  return AluOp::SLT;
                case Opcode::SLTIU: return AluOp::SLTU;
                case Opcode::ANDI:  return AluOp::AND;
                case Opcode::ORI:   return AluOp::OR;
                case Opcode::XORI:  return AluOp::XOR;
                case Opcode::LUI:   return AluOp::LUI;
                case Opcode::LW:
                case Opcode::LBU:
                case Opcode::LHU:
                case Opcode::SW:    return AluOp::ADDU;   // effective address
                case Opcode::BEQ:
                case Opcode::BNE:   return AluOp::SUBU;   // compare via zero flag
                default:            return std::nullopt;
            }

        case InstrFormat::J:
            // J and JAL form their target as { PC[31:28], target, 2'b00 }.
            // No ALU involved — the CPU stage handles it.
        default:
            return std::nullopt;
    }
}

// ─── Signed overflow helpers ──────────────────────────────────────────────────
// Overflow occurs when operands have a sign relationship the result violates.
// Using bit manipulation avoids signed UB and mirrors hardware carry logic.

// Addition overflows when both inputs share a sign that the result doesn't.
static constexpr bool overflow_add(uint32_t a, uint32_t b, uint32_t r) noexcept {
    //  ~(a ^ b)  : 1 where a and b have the SAME sign bit
    //   (a ^ r)  : 1 where a and result have DIFFERENT sign bits
    //  AND + MSB : overflow iff both conditions hold at bit 31
    return static_cast<bool>((~(a ^ b) & (a ^ r)) >> 31);
}

// Subtraction (a - b) overflows when signs differ and result has the wrong sign.
static constexpr bool overflow_sub(uint32_t a, uint32_t b, uint32_t r) noexcept {
    //  (a ^ b)   : 1 where a and b have DIFFERENT sign bits (subtraction can overflow)
    //  (a ^ r)   : 1 where result has the opposite sign from a (wrong outcome)
    return static_cast<bool>(((a ^ b) & (a ^ r)) >> 31);
}

// ─── Alu::execute ─────────────────────────────────────────────────────────────
AluResult Alu::execute(AluOp op, uint32_t a, uint32_t b, uint8_t shamt) {
    uint32_t value    = 0;
    bool     overflow = false;

    switch (op) {
        case AluOp::ADD: {
            value    = a + b;
            overflow = overflow_add(a, b, value);
            break;
        }
        case AluOp::ADDU: {
            value = a + b;
            // Intentionally no overflow check — ADDU wraps silently
            break;
        }
        case AluOp::SUB: {
            value    = a - b;
            overflow = overflow_sub(a, b, value);
            break;
        }
        case AluOp::SUBU: {
            value = a - b;
            break;
        }
        case AluOp::AND:  { value = a & b;   break; }
        case AluOp::OR:   { value = a | b;   break; }
        case AluOp::XOR:  { value = a ^ b;   break; }
        case AluOp::NOR:  { value = ~(a|b);  break; }

        case AluOp::SLT: {
            // Signed less-than: cast to int32_t so the comparison is signed
            value = (static_cast<int32_t>(a) < static_cast<int32_t>(b)) ? 1u : 0u;
            break;
        }
        case AluOp::SLTU: {
            // Unsigned less-than: uint32_t comparison is already unsigned
            value = (a < b) ? 1u : 0u;
            break;
        }

        case AluOp::SLL: {
            // Mask shamt to 5 bits — shifting by >= 32 is UB in C++
            value = b << (shamt & 0x1Fu);
            break;
        }
        case AluOp::SRL: {
            value = b >> (shamt & 0x1Fu);
            break;
        }
        case AluOp::SRA: {
            // C++20 guarantees two's complement, so right-shift on a signed
            // type is arithmetic (sign-extending). Cast, shift, cast back.
            value = static_cast<uint32_t>(
                        static_cast<int32_t>(b) >> (shamt & 0x1Fu));
            break;
        }

        case AluOp::LUI: {
            // Place the 16-bit immediate in the upper half; lower 16 become 0
            value = b << 16u;
            break;
        }
        case AluOp::PASS_A: {
            // Used by JR and JALR — return rs so the CPU can redirect the PC
            value = a;
            break;
        }
    }

    return AluResult{
        .value    = value,
        .zero     = (value == 0u),
        .overflow = overflow,
        .negative = static_cast<bool>(value >> 31u),
    };
}

} // namespace mips