// disasm_test.cpp — unit tests for the disassembler and the hex program loader.
//
// These two units were previously embedded in the FTXUI layer (src/nsc/ui.cpp)
// and therefore untestable. They now live in mips_core; this file gives them
// the /src ↔ /tests parity the rest of the core has.
//
// Lightweight harness — no external dependencies. Build via CMake target
// disasm_test, or directly:
//   g++ -std=c++20 -Iinclude src/mips/*.cpp tests/mips/disasm_test.cpp -o disasm_test

#include "mips/decoder.h"
#include "mips/disassembler.h"
#include "mips/program_loader.h"

#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>

using namespace mips;

static int g_passed = 0, g_failed = 0;

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (expr) {                                                                                \
            ++g_passed;                                                                            \
        } else {                                                                                   \
            ++g_failed;                                                                            \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr);                          \
        }                                                                                          \
    } while (0)

#define CHECK_EQ(a, b)                                                                             \
    do {                                                                                           \
        const auto va_ = (a);                                                                      \
        const auto vb_ = (b);                                                                      \
        if (va_ == vb_) {                                                                          \
            ++g_passed;                                                                            \
        } else {                                                                                   \
            ++g_failed;                                                                            \
            std::printf("  FAIL %s:%d  %s != %s\n", __FILE__, __LINE__, #a, #b);                   \
        }                                                                                          \
    } while (0)

// ── Encoding helpers (H&H Appendix B) ─────────────────────────────────────────
static uint32_t enc_r(uint8_t rs, uint8_t rt, uint8_t rd, uint8_t sh, uint8_t funct) {
    return (0u << 26) | (uint32_t(rs) << 21) | (uint32_t(rt) << 16) | (uint32_t(rd) << 11) |
           (uint32_t(sh) << 6) | uint32_t(funct);
}
static uint32_t enc_i(uint8_t op, uint8_t rs, uint8_t rt, uint16_t imm) {
    return (uint32_t(op) << 26) | (uint32_t(rs) << 21) | (uint32_t(rt) << 16) | uint32_t(imm);
}
static uint32_t enc_j(uint8_t op, uint32_t target) {
    return (uint32_t(op) << 26) | (target & 0x03FF'FFFFu);
}

static std::string dis(uint32_t word, uint32_t pc = 0) {
    const auto d = Decoder::decode(word);
    if (!d) return "<undecodable>";
    return Disassembler::to_string(*d, pc);
}

// ── Disassembler tests ────────────────────────────────────────────────────────
static void test_disasm_rtype() {
    // add $t2, $t0, $t1   (rd=10, rs=8, rt=9, funct ADD=0x20)
    CHECK_EQ(dis(enc_r(8, 9, 10, 0, 0x20)), std::string("add $t2, $t0, $t1"));
    // sub $s0, $s1, $s2   (rd=16, rs=17, rt=18, funct SUB=0x22)
    CHECK_EQ(dis(enc_r(17, 18, 16, 0, 0x22)), std::string("sub $s0, $s1, $s2"));
    // sll $t0, $t1, 4     (rd=8, rt=9, shamt=4, funct SLL=0x00)
    CHECK_EQ(dis(enc_r(0, 9, 8, 4, 0x00)), std::string("sll $t0, $t1, 4"));
    // sllv $t0, $t1, $t2  (rd=8, rt=9, rs=10, funct SLLV=0x04)
    CHECK_EQ(dis(enc_r(10, 9, 8, 0, 0x04)), std::string("sllv $t0, $t1, $t2"));
    // jr $ra              (rs=31, funct JR=0x08)
    CHECK_EQ(dis(enc_r(31, 0, 0, 0, 0x08)), std::string("jr $ra"));
    // jalr $ra, $t0       (rd=31, rs=8, funct JALR=0x09)
    CHECK_EQ(dis(enc_r(8, 0, 31, 0, 0x09)), std::string("jalr $ra, $t0"));
}

static void test_disasm_itype() {
    // addi $t0, $t1, 5    (op ADDI=0x08, rs=9, rt=8, imm=5)
    CHECK_EQ(dis(enc_i(0x08, 9, 8, 5)), std::string("addi $t0, $t1, +5"));
    // addi $t0, $t1, -1   (sign-extended immediate)
    CHECK_EQ(dis(enc_i(0x08, 9, 8, 0xFFFF)), std::string("addi $t0, $t1, -1"));
    // lw $t0, -4($sp)     (op LW=0x23, rs=29, rt=8, imm=0xFFFC)
    CHECK_EQ(dis(enc_i(0x23, 29, 8, 0xFFFC)), std::string("lw $t0, -4($sp)"));
    // sw $a0, 8($sp)      (op SW=0x2B, rs=29, rt=4, imm=8)
    CHECK_EQ(dis(enc_i(0x2B, 29, 4, 8)), std::string("sw $a0, 8($sp)"));
    // lui $t0, 0x1234     (op LUI=0x0F, rt=8, imm=0x1234)
    CHECK_EQ(dis(enc_i(0x0F, 0, 8, 0x1234)), std::string("lui $t0, 0x1234"));
    // beq $t0, $t1, -1    (op BEQ=0x04, rs=8, rt=9, imm=0xFFFF)
    CHECK_EQ(dis(enc_i(0x04, 8, 9, 0xFFFF)), std::string("beq $t0, $t1, -1"));
}

static void test_disasm_jtype() {
    // j 0x00400000 : target<<2 = 0x00400000 ⇒ target = 0x00100000, op J=0x02
    CHECK_EQ(dis(enc_j(0x02, 0x0010'0000u), 0x0040'0000u), std::string("j 0x00400000"));
    // jal target, high nibble taken from PC+4
    CHECK_EQ(dis(enc_j(0x03, 0x0010'0000u), 0x0040'0000u), std::string("jal 0x00400000"));
}

static void test_disasm_undecodable() {
    // Opcode 0x3F is not in the supported set → decoder returns nullopt.
    CHECK_EQ(dis(0xFC00'0000u), std::string("<undecodable>"));
}

// ── Program-loader tests ──────────────────────────────────────────────────────
static void test_loader_basic() {
    std::istringstream in("0x00000020\n0xDEADBEEF\n08\n");
    const HexProgram   p = parse_hex_program(in);
    CHECK(p.ok());
    CHECK_EQ(p.words.size(), std::size_t{3});
    CHECK_EQ(p.words[0], 0x0000'0020u);
    CHECK_EQ(p.words[1], 0xDEAD'BEEFu);
    CHECK_EQ(p.words[2], 0x0000'0008u);
}

static void test_loader_comments_and_blanks() {
    std::istringstream in("# header comment\n"
                          "\n"
                          "  0x10   # inline comment\n"
                          "   \n"
                          "20\n");
    const HexProgram   p = parse_hex_program(in);
    CHECK(p.ok());
    CHECK_EQ(p.words.size(), std::size_t{2});
    CHECK_EQ(p.words[0], 0x10u);
    CHECK_EQ(p.words[1], 0x20u);
}

static void test_loader_bad_hex_reports_line() {
    std::istringstream in("0x01\n"
                          "not_hex\n"
                          "0x03\n");
    const HexProgram   p = parse_hex_program(in);
    CHECK(!p.ok());
    CHECK(p.words.empty());
    CHECK(p.error.has_value());
    CHECK(p.error->find("line 2") != std::string::npos);
}

static void test_loader_trailing_garbage_rejected() {
    // "12xy" must not silently parse as 0x12 — the whole token must be hex.
    std::istringstream in("12xy\n");
    const HexProgram   p = parse_hex_program(in);
    CHECK(!p.ok());
}

static void test_loader_empty_is_valid() {
    std::istringstream in("# only comments\n\n");
    const HexProgram   p = parse_hex_program(in);
    CHECK(p.ok());
    CHECK(p.words.empty());
}

int main() {
    test_disasm_rtype();
    test_disasm_itype();
    test_disasm_jtype();
    test_disasm_undecodable();
    test_loader_basic();
    test_loader_comments_and_blanks();
    test_loader_bad_hex_reports_line();
    test_loader_trailing_garbage_rejected();
    test_loader_empty_is_valid();

    std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
