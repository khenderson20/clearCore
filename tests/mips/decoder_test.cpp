// Lightweight test harness — no external dependencies needed.
// Build:  cmake ... && cmake --build ... --target decoder_test
// Run:    ./cmake-build-debug/decoder_test
//
// Each test encodes a real MIPS instruction and verifies that the
// decoded fields match the bit layout from H&H Appendix B.

#include "mips/decoder.h"

#include <cassert>
#include <cstdio>

using namespace mips;

static int g_passed = 0, g_failed = 0;

#define CHECK(expr)                                                         \
    do {                                                                    \
        if (expr) {                                                         \
            ++g_passed;                                                     \
        } else {                                                            \
            std::fprintf(stderr, "FAIL  %s:%d  %s\n",                      \
                         __FILE__, __LINE__, #expr);                        \
            ++g_failed;                                                     \
        }                                                                   \
    } while (false)

// ─── R-type: ADD $t0, $s0, $s1 ───────────────────────────────────────────────
// opcode=0 | rs=$s0(16) | rt=$s1(17) | rd=$t0(8) | shamt=0 | funct=0x20
// Binary:  000000 10000 10001 01000 00000 100000
// Hex:     0x02114020
static void test_r_add() {
    auto result = Decoder::decode(0x02114020u);
    CHECK(result.has_value());
    CHECK(result->format == InstrFormat::R);
    CHECK(Decoder::mnemonic(*result) == "ADD");
    CHECK(result->r().rs    == 16);   // $s0
    CHECK(result->r().rt    == 17);   // $s1
    CHECK(result->r().rd    == 8);    // $t0
    CHECK(result->r().shamt == 0);
    CHECK(result->r().funct == FunctCode::ADD);
}

// ─── R-type: SLL $t2, $t1, 4 ─────────────────────────────────────────────────
// opcode=0 | rs=0 | rt=$t1(9) | rd=$t2(10) | shamt=4 | funct=0x00
// Binary:  000000 00000 01001 01010 00100 000000
// Hex:     0x00095100
static void test_r_sll() {
    auto result = Decoder::decode(0x00095100u);
    CHECK(result.has_value());
    CHECK(result->format == InstrFormat::R);
    CHECK(Decoder::mnemonic(*result) == "SLL");
    CHECK(result->r().rs    == 0);
    CHECK(result->r().rt    == 9);    // $t1
    CHECK(result->r().rd    == 10);   // $t2
    CHECK(result->r().shamt == 4);
    CHECK(result->r().funct == FunctCode::SLL);
}

// ─── I-type: LW $t0, 4($sp) ──────────────────────────────────────────────────
// opcode=0x23 | rs=$sp(29) | rt=$t0(8) | imm=4
// Binary:  100011 11101 01000 0000000000000100
// Hex:     0x8FA80004
static void test_i_lw() {
    auto result = Decoder::decode(0x8FA80004u);
    CHECK(result.has_value());
    CHECK(result->format == InstrFormat::I);
    CHECK(result->opcode  == Opcode::LW);
    CHECK(Decoder::mnemonic(*result) == "LW");
    CHECK(result->i().rs  == 29);     // $sp
    CHECK(result->i().rt  == 8);      // $t0
    CHECK(result->i().imm == 4);
}

// ─── I-type: BEQ $t0, $zero, -2 ─────────────────────────────────────────────
// Simulates a backward loop branch. Raw imm=0xFFFE = −2 signed.
// opcode=0x04 | rs=$t0(8) | rt=$zero(0) | imm=0xFFFE
// Binary:  000100 01000 00000 1111111111111110
// Hex:     0x1100FFFE
static void test_i_beq_negative() {
    auto result = Decoder::decode(0x1100FFFEu);
    CHECK(result.has_value());
    CHECK(result->format == InstrFormat::I);
    CHECK(result->opcode  == Opcode::BEQ);
    CHECK(result->i().rs  == 8);        // $t0
    CHECK(result->i().rt  == 0);        // $zero
    CHECK(result->i().imm == 0xFFFE);   // raw bits preserved

    // The EX stage calls sign_extend; verify the branch offset is correct
    CHECK(Decoder::sign_extend(result->i().imm) == -2);
}

// ─── J-type: J 0x100000 ──────────────────────────────────────────────────────
// opcode=0x02 | target=0x100000
// Binary:  000010 00000100000000000000000000
// Hex:     0x08100000
static void test_j_jump() {
    auto result = Decoder::decode(0x08100000u);
    CHECK(result.has_value());
    CHECK(result->format   == InstrFormat::J);
    CHECK(result->opcode   == Opcode::J);
    CHECK(Decoder::mnemonic(*result) == "J");
    CHECK(result->j().target == 0x100000u);
}

// ─── sign_extend edge cases ───────────────────────────────────────────────────
static void test_sign_extend() {
    CHECK(Decoder::sign_extend(0x0000) ==      0);
    CHECK(Decoder::sign_extend(0x0001) ==      1);
    CHECK(Decoder::sign_extend(0x7FFF) ==  32767);   // largest positive
    CHECK(Decoder::sign_extend(0x8000) == -32768);   // most negative (sign bit set)
    CHECK(Decoder::sign_extend(0xFFFF) ==     -1);
}

// ─── Unknown / reserved encodings ────────────────────────────────────────────
static void test_unknown() {
    // Opcode 0x3F is not in the MIPS I ISA
    CHECK(!Decoder::decode(0xFC000000u).has_value());

    // SPECIAL opcode with a reserved funct code (0x01)
    CHECK(!Decoder::decode(0x00000001u).has_value());
}

// ─── format_of ────────────────────────────────────────────────────────────────
static void test_format_of() {
    CHECK(Decoder::format_of(Opcode::SPECIAL) == InstrFormat::R);
    CHECK(Decoder::format_of(Opcode::J)       == InstrFormat::J);
    CHECK(Decoder::format_of(Opcode::JAL)     == InstrFormat::J);
    CHECK(Decoder::format_of(Opcode::LW)      == InstrFormat::I);
    CHECK(Decoder::format_of(Opcode::BEQ)     == InstrFormat::I);
    CHECK(Decoder::format_of(Opcode::SW)      == InstrFormat::I);
}

// ──────────────────────────────────────────────────────────────────────────────
int main() {
    test_r_add();
    test_r_sll();
    test_i_lw();
    test_i_beq_negative();
    test_j_jump();
    test_sign_extend();
    test_unknown();
    test_format_of();

    std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return (g_failed > 0) ? 1 : 0;
}
