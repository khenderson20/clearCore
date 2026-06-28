#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>

namespace mips {

// ─── Instruction format ───────────────────────────────────────────────────────
// Harris & Harris §6.3: all three MIPS formats are exactly 32 bits wide.
// The opcode field [31:26] determines which format applies.
enum class InstrFormat : uint8_t { R, I, J, Unknown };

// ─── Opcode field [31:26] ─────────────────────────────────────────────────────
enum class Opcode : uint8_t {
    SPECIAL = 0x00,   // R-type: dispatch further on funct [5:0]
    J       = 0x02,
    JAL     = 0x03,
    BEQ     = 0x04,
    BNE     = 0x05,
    ADDI    = 0x08,
    ADDIU   = 0x09,
    SLTI    = 0x0A,
    SLTIU   = 0x0B,
    ANDI    = 0x0C,
    ORI     = 0x0D,
    XORI    = 0x0E,
    LUI     = 0x0F,
    LW      = 0x23,
    LBU     = 0x24,
    LHU     = 0x25,
    SW      = 0x2B,
};

// ─── Funct field [5:0] ────────────────────────────────────────────────────────
// Only meaningful when opcode == SPECIAL (0x00).
enum class FunctCode : uint8_t {
    SLL  = 0x00,
    SRL  = 0x02,
    SRA  = 0x03,
    SLLV = 0x04,
    SRLV = 0x06,
    JR   = 0x08,
    JALR = 0x09,
    ADD  = 0x20,
    ADDU = 0x21,
    SUB  = 0x22,
    SUBU = 0x23,
    AND  = 0x24,
    OR   = 0x25,
    XOR  = 0x26,
    NOR  = 0x27,
    SLT  = 0x2A,
    SLTU = 0x2B,
};

// ─── Per-format field structs (H&H Figure 6.10) ───────────────────────────────
// Each mirrors the hardware bit layout exactly.

struct RFields {
    uint8_t   rs;     // [25:21] first source register
    uint8_t   rt;     // [20:16] second source register
    uint8_t   rd;     // [15:11] destination register
    uint8_t   shamt;  // [10:6]  shift amount
    FunctCode funct;  // [5:0]   function code
};

struct IFields {
    uint8_t  rs;   // [25:21] base or source register
    uint8_t  rt;   // [20:16] destination register
    uint16_t imm;  // [15:0]  raw immediate — sign extension is EX-stage work
};

struct JFields {
    uint32_t target;  // [25:0]  jump target (×4 + PC[31:28] in hardware)
};

// ─── Decoded instruction ──────────────────────────────────────────────────────

struct DecodedInstr {
    uint32_t    raw;     // Original 32-bit word — preserved for TUI display
    Opcode      opcode;  // [31:26]
    InstrFormat format;  // Derived from opcode

    // std::variant enforces which field struct is active.
    // Accessing the wrong member throws std::bad_variant_access, making
    // datapath bugs loud rather than silently reading garbage.
    std::variant<RFields, IFields, JFields> fields;

    // Convenience accessors — throw if format doesn't match
    [[nodiscard]] const RFields& r() const { return std::get<RFields>(fields); }
    [[nodiscard]] const IFields& i() const { return std::get<IFields>(fields); }
    [[nodiscard]] const JFields& j() const { return std::get<JFields>(fields); }
};

// ─── Decoder ──────────────────────────────────────────────────────────────────

class Decoder {
public:
    // Decode a 32-bit instruction word.
    // Returns std::nullopt for any unrecognized opcode or funct code.
    [[nodiscard]] static std::optional<DecodedInstr> decode(uint32_t instr);

    // Human-readable mnemonic: "ADD", "LW", "BEQ", etc.
    // Returns "???" for unrecognized encodings.
    [[nodiscard]] static std::string_view mnemonic(const DecodedInstr& instr);

    // Derive InstrFormat from opcode alone.
    [[nodiscard]] static InstrFormat format_of(Opcode op);

    // Sign-extend a raw 16-bit immediate to 32 bits.
    // Convenience for the EX stage; not called during decode itself.
    [[nodiscard]] static int32_t sign_extend(uint16_t imm);

private:
    // Extract bits [hi:lo] from a 32-bit word.
    // Models the wire taps on a datapath schematic: point at a field,
    // get back the integer it holds.
    [[nodiscard]] static constexpr uint32_t bits(uint32_t word,
                                                  int hi, int lo) noexcept {
        return (word >> lo) & ((1u << (hi - lo + 1)) - 1);
    }
};

} // namespace mips