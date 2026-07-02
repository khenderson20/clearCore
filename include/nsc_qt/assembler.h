#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace nsc::qt {

struct AssemblerResult {
    std::vector<uint32_t>      words;
    std::optional<std::string> error;  // "line N: message" on failure

    [[nodiscard]] bool ok() const noexcept { return !error.has_value(); }
    explicit           operator bool() const noexcept { return ok(); }
};

// Assemble a multi-line MIPS assembly source string into 32-bit words.
// Supported instructions: add, addu, sub, subu, and, or, xor, nor, slt, sltu,
//   sll, srl, sra, sllv, srlv, jr, jalr, addi, addiu, slti, sltiu, andi,
//   ori, xori, lui, lw, lbu, lhu, sw, beq, bne, j, jal, nop.
// Labels (name:) are supported as targets for branches and jumps.
// Comments: # to end of line. Blank lines and pure-comment lines are ignored.
[[nodiscard]] AssemblerResult assemble(const std::string& source);

}  // namespace nsc::qt
