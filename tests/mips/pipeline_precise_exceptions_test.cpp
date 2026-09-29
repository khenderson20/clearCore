// Pipelined exception precision: squashed (wrong-path) and younger instructions
// must never trap or redirect. SingleCycleCpu has no such hazard and is the
// oracle: every program here must behave identically on both models.
// Regression tests for the ordering bugs described in issue #218.

#include "mips/pipelined_cpu.h"
#include "mips/single_cycle_cpu.h"

#include <cstdint>
#include <cstdio>
#include <vector>

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

namespace enc {
constexpr uint32_t R(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shamt, uint32_t funct) {
    return (rs << 21) | (rt << 16) | (rd << 11) | (shamt << 6) | funct;
}
constexpr uint32_t I(uint32_t op, uint32_t rs, uint32_t rt, uint16_t imm) {
    return (op << 26) | (rs << 21) | (rt << 16) | imm;
}
constexpr uint32_t zero = 0, t0 = 8, t1 = 9;
constexpr uint32_t ADDI = 0x08, BEQ = 0x04, LW = 0x23;
constexpr uint32_t F_JR     = 0x08;
constexpr uint32_t SYSCALL  = 0x0C;
constexpr uint32_t kIllegal = 0xFFFF'FFFFu;  // opcode 0x3F: not decodable

// `j` to its own address: the project's halt idiom.
constexpr uint32_t halt_at(uint32_t byte_addr) {
    return (0x02u << 26) | ((byte_addr >> 2) & 0x03FF'FFFFu);
}
}  // namespace enc

// Run to the first non-Ok result (the cap guards against a runaway program).
static mips::StepResult run_to_stop(mips::IProcessor& cpu) {
    for (int i = 0; i < 64; ++i) {
        const mips::StepResult r = cpu.step();
        if (r != mips::StepResult::Ok) return r;
    }
    return mips::StepResult::Ok;
}

template <typename Setup> static void expect_both_halt(Setup&& setup, std::size_t mem_bytes) {
    mips::SingleCycleCpu sc(mem_bytes);
    mips::PipelinedCpu   pl(mem_bytes);
    setup(sc);
    setup(pl);
    CHECK(run_to_stop(sc) == mips::StepResult::Halt);  // oracle
    CHECK(run_to_stop(pl) == mips::StepResult::Halt);
}

// A taken branch squashes the word after it; if that word is undecodable it
// must not raise a Reserved Instruction exception.
static void test_taken_branch_over_illegal_word() {
    using namespace enc;
    expect_both_halt(
        [](mips::IProcessor& cpu) {
            // beq $0,$0,+1 -> 8; word at 4 is skipped data.
            CHECK(cpu.load_program({I(BEQ, zero, zero, 1), kIllegal, halt_at(8)}, 0));
        },
        1u << 12);
}

// Same for `jr`: data placed after a return must never trap.
static void test_jr_over_illegal_words() {
    using namespace enc;
    expect_both_halt(
        [](mips::IProcessor& cpu) {
            CHECK(cpu.load_program({I(ADDI, zero, t0, 16),  // t0 = 16
                                    R(t0, 0, 0, 0, F_JR),   // jr $t0
                                    kIllegal, kIllegal, halt_at(16)},
                                   0));
        },
        1u << 12);
}

// The fall-through fetch of a taken branch may run past the end of memory; that
// wrong-path fetch must not raise AdEL.
static void test_taken_branch_wrong_path_fetch_past_end() {
    using namespace enc;
    expect_both_halt(
        [](mips::IProcessor& cpu) {
            // 32-byte memory. halt at 0; `beq $0,$0,-7` at 24 -> 28 + (-7*4) = 0.
            // While the branch is in EX the pipeline has already fetched 28 and 32 (OOB).
            CHECK(cpu.load_program({halt_at(0)}, 0));
            CHECK(cpu.mem().write_word(24, I(BEQ, zero, zero, 0xFFF9)));
            cpu.set_pc(24);
        },
        32);
}

// A faulting load (MEM) with a SYSCALL right behind it (EX): the older fault
// must be the one reported. The SYSCALL is squashed and never raises.
static void test_older_fault_wins_over_younger_syscall() {
    using namespace enc;
    const std::vector<uint32_t> prog = {I(ADDI, zero, t0, 1),  // t0 = 1 (misaligned)
                                        I(LW, t0, t1, 0),      // AdEL, BadVAddr = 1
                                        SYSCALL, halt_at(12)};
    mips::SingleCycleCpu        sc(1u << 12);
    mips::PipelinedCpu          pl(1u << 12);
    for (mips::IProcessor* cpu :
         {static_cast<mips::IProcessor*>(&sc), static_cast<mips::IProcessor*>(&pl)}) {
        CHECK(cpu->load_program(prog, 0));
        CHECK(run_to_stop(*cpu) == mips::StepResult::Exception);
    }
    CHECK(sc.cp0().last_exception() == mips::ExceptionCode::AdEL);  // oracle
    CHECK(pl.cp0().last_exception() == mips::ExceptionCode::AdEL);
    CHECK(pl.cp0().bad_vaddr() == 1u);
    CHECK(pl.cp0().epc() == 4u);
}

int main() {
    test_taken_branch_over_illegal_word();
    test_jr_over_illegal_words();
    test_taken_branch_wrong_path_fetch_past_end();
    test_older_fault_wins_over_younger_syscall();

    std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
