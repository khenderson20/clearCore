#pragma once

#include "mips/decoder.h"

#include <cstdint>
#include <optional>

namespace mips {

// ─── ALU operation codes ──────────────────────────────────────────────────────
// These are what the ALU actually performs — not ISA opcodes.
// The control() function is the bridge between a DecodedInstr and one of these.
// H&H calls this the "4-bit ALU control signal" (§7.3, Figure 7.10).
enum class AluOp : uint8_t {
    ADD,   // Signed addition   — sets overflow flag on signed overflow
    ADDU,  // Unsigned addition — overflow flag always false
    SUB,   // Signed subtraction — sets overflow flag
    SUBU,  // Unsigned subtraction — overflow flag always false
    AND,
    OR,
    XOR,
    NOR,
    SLT,     // Set if a < b (signed)   → result is 0 or 1
    SLTU,    // Set if a < b (unsigned) → result is 0 or 1
    SLL,     // Shift left logical:           b << shamt
    SRL,     // Shift right logical (zero):   b >> shamt
    SRA,     // Shift right arithmetic (sign): b >> shamt, sign-extending
    LUI,     // Load upper immediate: b << 16  (operand a is unused)
    PASS_A,  // Pass a through unchanged — used by JR and JALR
};

// ─── ALU result ───────────────────────────────────────────────────────────────
// Models the output wires of the ALU in a datapath diagram.
// The CPU stage reads these to drive branch control, writeback, and exceptions.
struct AluResult {
    uint32_t value;     // The computed 32-bit result
    bool     zero;      // value == 0; drives BEQ/BNE branch logic
    bool     overflow;  // Signed overflow; only set by ADD and SUB
    bool     negative;  // MSB of value; available for future signed operations
};

// ─── Alu ──────────────────────────────────────────────────────────────────────
class Alu {
public:
    // Execute an ALU operation on two 32-bit operands.
    //
    // a     — first operand  (register rs in the datapath)
    // b     — second operand (rt for R-type, sign_extend(imm) for I-type;
    //         zero_extend(imm) for ANDI/ORI/XORI — the caller selects)
    // shamt — shift amount; the caller is responsible for the source:
    //           SLL/SRL/SRA  → instr.r().shamt  (instruction field [10:6])
    //           SLLV/SRLV    → reg[rs] & 0x1F   (lower 5 bits of rs register)
    //         The ALU itself just shifts by whatever value it receives.
    [[nodiscard]] static AluResult execute(AluOp op, uint32_t a, uint32_t b, uint8_t shamt = 0);

    // ALU control block — maps a decoded instruction to an AluOp.
    //
    // This is the "ALU control" combinational block in H&H Figure 7.10.
    // It looks at opcode (and funct for R-type) and selects the operation.
    //
    // Returns std::nullopt for J and JAL — those instructions compute their
    // target address directly in the CPU stage without using the ALU.
    [[nodiscard]] static std::optional<AluOp> control(const DecodedInstr& instr);
};

}  // namespace mips
