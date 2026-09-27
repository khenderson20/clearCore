// Lightweight test harness — no external dependencies needed.
// Build:  cmake ... && cmake --build ... --target cpu_test
// Run:    ./cmake-build-debug/cpu_test
//
// Each test hand-assembles a small MIPS program with the encode helpers below,
// runs it to its halt (a self-targeting jump), and verifies the resulting
// register / memory state. Encodings follow H&H Appendix B.

#include "mips/cpu.h"
#include "mips/decoder.h"

#include <cstdint>
#include <cstdio>
#include <vector>

using namespace mips;

static int g_passed = 0, g_failed = 0;

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (expr) {                                                                                \
            ++g_passed;                                                                            \
        } else {                                                                                   \
            std::fprintf(stderr, "FAIL  %s:%d  %s\n", __FILE__, __LINE__, #expr);                  \
            ++g_failed;                                                                            \
        }                                                                                          \
    } while (false)

// ─── Instruction encoders ─────────────────────────────────────────────────────
namespace enc {
constexpr uint32_t R(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shamt, uint32_t funct) {
    return (rs << 21) | (rt << 16) | (rd << 11) | (shamt << 6) | funct;
}
constexpr uint32_t I(uint32_t op, uint32_t rs, uint32_t rt, uint16_t imm) {
    return (op << 26) | (rs << 21) | (rt << 16) | imm;
}
constexpr uint32_t J(uint32_t op, uint32_t target) {
    return (op << 26) | (target & 0x03FF'FFFFu);
}

// opcodes
constexpr uint32_t ADDI = 0x08, ADDIU = 0x09, SLTI = 0x0A, ANDI = 0x0C, ORI = 0x0D, LUI = 0x0F,
                   LW = 0x23, SW = 0x2B, BEQ = 0x04, BNE = 0x05, JOP = 0x02, JAL = 0x03;
// funct
constexpr uint32_t F_ADD = 0x20, F_SUB = 0x22, F_AND = 0x24, F_OR = 0x25, F_SLT = 0x2A, F_JR = 0x08;
// registers
constexpr uint32_t zero = 0, v0 = 2, a0 = 4, t0 = 8, t1 = 9, t2 = 10, t3 = 11, t4 = 12, t5 = 13,
                   t6 = 14, t7 = 15, s0 = 16, ra = 31;
}  // namespace enc

// ─── Arithmetic / logic: addi, add, sub, and, or, slt ─────────────────────────
static void test_arithmetic() {
    using namespace enc;
    std::vector<uint32_t> prog = {
        I(ADDI, zero, t0, 10),    // t0 = 10
        I(ADDI, zero, t1, 20),    // t1 = 20
        R(t0, t1, t2, 0, F_ADD),  // t2 = 30
        R(t1, t0, t3, 0, F_SUB),  // t3 = 10
        R(t0, t1, t4, 0, F_AND),  // t4 = 10 & 20 = 0
        R(t0, t1, t5, 0, F_OR),   // t5 = 10 | 20 = 30
        R(t0, t1, t6, 0, F_SLT),  // t6 = (10 < 20) = 1
        R(t1, t0, t7, 0, F_SLT),  // t7 = (20 < 10) = 0
        J(JOP, 8),                // 8th word (addr 32) — j self → halt
    };
    Cpu cpu;
    CHECK(cpu.load_program(prog));
    CHECK(cpu.run() == StepResult::Halt);
    CHECK(cpu.regs().read(t2) == 30);
    CHECK(cpu.regs().read(t3) == 10);
    CHECK(cpu.regs().read(t4) == 0);
    CHECK(cpu.regs().read(t5) == 30);
    CHECK(cpu.regs().read(t6) == 1);
    CHECK(cpu.regs().read(t7) == 0);
}

// ─── Memory round-trip: sw then lw ────────────────────────────────────────────
static void test_load_store() {
    using namespace enc;
    std::vector<uint32_t> prog = {
        I(ADDI, zero, t0, 291),   // t0 = 291
        I(ADDI, zero, s0, 4096),  // base = 4096 (data region, past code)
        I(SW, s0, t0, 0),         // mem[4096] = 291
        I(LW, s0, t1, 0),         // t1 = mem[4096]
        I(ADDI, t1, t2, 9),       // t2 = 300
        I(SW, s0, t2, 4),         // mem[4100] = 300
        J(JOP, 6),                // addr 24 — j self → halt
    };
    Cpu cpu;
    CHECK(cpu.load_program(prog));
    CHECK(cpu.run() == StepResult::Halt);
    CHECK(cpu.regs().read(t1) == 291);
    CHECK(cpu.regs().read(t2) == 300);
    CHECK(cpu.mem().read_word(4096) == std::optional<uint32_t>{291});
    CHECK(cpu.mem().read_word(4100) == std::optional<uint32_t>{300});
}

// ─── Forward branch + jump loop: sum 1..5 = 15 ────────────────────────────────
static void test_branch_loop() {
    using namespace enc;
    //  0: addi t0,zero,0     sum = 0
    //  4: addi t1,zero,1     i = 1
    //  8: addi t2,zero,6     limit = 6
    // 12: beq  t1,t2,+3      if i == 6 -> end (addr 28)
    // 16: add  t0,t0,t1      sum += i
    // 20: addi t1,t1,1       i++
    // 24: j    3             -> loop (addr 12)
    // 28: j    7             -> self (halt)
    std::vector<uint32_t> prog = {
        I(ADDI, zero, t0, 0),
        I(ADDI, zero, t1, 1),
        I(ADDI, zero, t2, 6),
        I(BEQ, t1, t2, 3),
        R(t0, t1, t0, 0, F_ADD),
        I(ADDI, t1, t1, 1),
        J(JOP, 3),
        J(JOP, 7),
    };
    Cpu cpu;
    CHECK(cpu.load_program(prog));
    CHECK(cpu.run() == StepResult::Halt);
    CHECK(cpu.regs().read(t0) == 15);  // 1+2+3+4+5
    CHECK(cpu.regs().read(t1) == 6);
}

// ─── Function call: jal / jr with link register ───────────────────────────────
static void test_call_return() {
    using namespace enc;
    //  0: addi a0,zero,7
    //  4: jal  3            -> dbl (addr 12), $ra = 8
    //  8: j    5            -> done (addr 20)
    // 12: add  v0,a0,a0     v0 = 14
    // 16: jr   ra           -> 8
    // 20: j    5            -> self (halt)
    std::vector<uint32_t> prog = {
        I(ADDI, zero, a0, 7), J(JAL, 3), J(JOP, 5), R(a0, a0, v0, 0, F_ADD),
        R(ra, 0, 0, 0, F_JR), J(JOP, 5),
    };
    Cpu cpu;
    CHECK(cpu.load_program(prog));
    CHECK(cpu.run() == StepResult::Halt);
    CHECK(cpu.regs().read(v0) == 14);
    CHECK(cpu.regs().read(ra) == 8);  // return address captured by jal
}

// ─── 32-bit constant build: lui + ori ─────────────────────────────────────────
static void test_lui_ori() {
    using namespace enc;
    std::vector<uint32_t> prog = {
        I(LUI, zero, t0, 0xABCD),  // t0 = 0xABCD0000
        I(ORI, t0, t0, 0xEF01),    // t0 = 0xABCDEF01
        J(JOP, 2),                 // addr 8 — j self → halt
    };
    Cpu cpu;
    CHECK(cpu.load_program(prog));
    CHECK(cpu.run() == StepResult::Halt);
    CHECK(cpu.regs().read(t0) == 0xABCDEF01u);
}

// ─── Countdown with bne + negative immediate: acc = 50 ────────────────────────
static void test_bne_countdown() {
    using namespace enc;
    //  0: addi t0,zero,5     counter
    //  4: addi t1,zero,0     acc
    //  8: addi t1,t1,10      acc += 10        (loop)
    // 12: addi t0,t0,-1      counter--
    // 16: bne  t0,zero,-3    while counter != 0 -> loop (addr 8)
    // 20: j    6             -> end (addr 24)
    // 24: j    6             -> self (halt)
    std::vector<uint32_t> prog = {
        I(ADDI, zero, t0, 5),
        I(ADDI, zero, t1, 0),
        I(ADDI, t1, t1, 10),
        I(ADDI, t0, t0, static_cast<uint16_t>(-1)),
        I(BNE, t0, zero, static_cast<uint16_t>(-3)),
        J(JOP, 6),
        J(JOP, 6),
    };
    Cpu cpu;
    CHECK(cpu.load_program(prog));
    CHECK(cpu.run() == StepResult::Halt);
    CHECK(cpu.regs().read(t1) == 50);
    CHECK(cpu.regs().read(t0) == 0);
}

// ─── $zero stays hardwired, and faults are reported ───────────────────────────
static void test_zero_and_faults() {
    using namespace enc;
    // Writing to $zero must be a no-op.
    std::vector<uint32_t> prog = {
        I(ADDI, zero, zero, 5),  // attempt to set $zero = 5 (ignored)
        J(JOP, 1),               // addr 4 — j self → halt
    };
    Cpu cpu;
    CHECK(cpu.load_program(prog));
    CHECK(cpu.run() == StepResult::Halt);
    CHECK(cpu.regs().read(zero) == 0);

    // An undecodable opcode raises a Reserved Instruction (RI) exception.
    Cpu cpu2;
    CHECK(cpu2.load_program({0xFC00'0000u}));  // opcode 0x3F — not in ISA
    CHECK(cpu2.step() == StepResult::Exception);

    // Running past the end of memory raises an AdEL (address error on load) exception.
    Cpu cpu3(8);  // 8 bytes = 2 words
    CHECK(cpu3.load_program({I(ADDI, zero, t0, 1), I(ADDI, zero, t1, 2)}));
    CHECK(cpu3.step() == StepResult::Ok);         // addr 0
    CHECK(cpu3.step() == StepResult::Ok);         // addr 4
    CHECK(cpu3.step() == StepResult::Exception);  // addr 8 — AdEL
}

int main() {
    test_arithmetic();
    test_load_store();
    test_branch_loop();
    test_call_return();
    test_lui_ori();
    test_bne_countdown();
    test_zero_and_faults();

    std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return (g_failed > 0) ? 1 : 0;
}
