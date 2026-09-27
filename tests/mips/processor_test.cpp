// ─── processor_test.cpp ───────────────────────────────────────────────────────
// Validates the IProcessor contract against both concrete implementations.
//
// Every test function receives an IProcessor& and verifies the same final
// register/memory state regardless of whether the underlying engine is
// single-cycle or pipelined.  This is the key payoff of the Ripes/DrMIPS
// "pluggable-backend" pattern: the same correctness harness covers both.
//
// Build:   cmake --build ... --target processor_test
// Run:     ./cmake-build-debug/processor_test

#include "mips/cpu.h"  // for Cpu alias (backward-compat check)
#include "mips/pipelined_cpu.h"
#include "mips/processor.h"
#include "mips/single_cycle_cpu.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string_view>
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

// ─── Instruction encoders (same as cpu_test.cpp) ─────────────────────────────
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
constexpr uint32_t ADDI = 0x08, ADDIU = 0x09, ORI = 0x0D, LUI = 0x0F, LW = 0x23, SW = 0x2B,
                   BEQ = 0x04, BNE = 0x05, JOP = 0x02, JAL = 0x03;
constexpr uint32_t F_ADD = 0x20, F_SUB = 0x22, F_AND = 0x24, F_OR = 0x25, F_SLT = 0x2A, F_JR = 0x08;
constexpr uint32_t zero = 0, v0 = 2, a0 = 4, t0 = 8, t1 = 9, t2 = 10, t3 = 11, t4 = 12, t5 = 13,
                   t6 = 14, t7 = 15, s0 = 16, ra = 31;
}  // namespace enc

// ─── Tests — each operates through the abstract interface ─────────────────────

static void test_arithmetic(IProcessor& cpu, std::string_view label) {
    using namespace enc;
    cpu.reset(true);
    std::vector<uint32_t> prog = {
        I(ADDI, zero, t0, 10),
        I(ADDI, zero, t1, 20),
        R(t0, t1, t2, 0, F_ADD),  // t2 = 30
        R(t1, t0, t3, 0, F_SUB),  // t3 = 10
        R(t0, t1, t4, 0, F_AND),  // t4 = 0
        R(t0, t1, t5, 0, F_OR),   // t5 = 30
        R(t0, t1, t6, 0, F_SLT),  // t6 = 1
        R(t1, t0, t7, 0, F_SLT),  // t7 = 0
        J(JOP, 8),
    };
    CHECK(cpu.load_program(prog));
    CHECK(cpu.run() == StepResult::Halt);
    CHECK(cpu.regs().read(t2) == 30);
    CHECK(cpu.regs().read(t3) == 10);
    CHECK(cpu.regs().read(t4) == 0);
    CHECK(cpu.regs().read(t5) == 30);
    CHECK(cpu.regs().read(t6) == 1);
    CHECK(cpu.regs().read(t7) == 0);
    (void)label;
}

static void test_load_store(IProcessor& cpu, std::string_view label) {
    using namespace enc;
    cpu.reset(true);
    std::vector<uint32_t> prog = {
        I(ADDI, zero, t0, 291), I(ADDI, zero, s0, 4096), I(SW, s0, t0, 0), I(LW, s0, t1, 0),
        I(ADDI, t1, t2, 9),     I(SW, s0, t2, 4),        J(JOP, 6),
    };
    CHECK(cpu.load_program(prog));
    CHECK(cpu.run() == StepResult::Halt);
    CHECK(cpu.regs().read(t1) == 291);
    CHECK(cpu.regs().read(t2) == 300);
    CHECK(cpu.mem().read_word(4096) == std::optional<uint32_t>{291});
    CHECK(cpu.mem().read_word(4100) == std::optional<uint32_t>{300});
    (void)label;
}

static void test_branch_loop(IProcessor& cpu, std::string_view label) {
    using namespace enc;
    cpu.reset(true);
    //  0: sum=0   4: i=1   8: limit=6
    // 12: beq i,limit,+3 → 28
    // 16: sum+=i  20: i++  24: j 3 → 12   28: j self → halt
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
    CHECK(cpu.load_program(prog));
    CHECK(cpu.run() == StepResult::Halt);
    CHECK(cpu.regs().read(t0) == 15);  // 1+2+3+4+5
    CHECK(cpu.regs().read(t1) == 6);
    (void)label;
}

static void test_call_return(IProcessor& cpu, std::string_view label) {
    using namespace enc;
    cpu.reset(true);
    //  0: addi a0,0,7   4: jal 3→12  8: j 5→20  12: add v0,a0,a0  16: jr ra  20: j self
    std::vector<uint32_t> prog = {
        I(ADDI, zero, a0, 7), J(JAL, 3), J(JOP, 5), R(a0, a0, v0, 0, F_ADD),
        R(ra, 0, 0, 0, F_JR), J(JOP, 5),
    };
    CHECK(cpu.load_program(prog));
    CHECK(cpu.run() == StepResult::Halt);
    CHECK(cpu.regs().read(v0) == 14);
    CHECK(cpu.regs().read(ra) == 8);
    (void)label;
}

static void test_lui_ori(IProcessor& cpu, std::string_view label) {
    using namespace enc;
    cpu.reset(true);
    std::vector<uint32_t> prog = {
        I(LUI, zero, t0, 0xABCD),
        I(ORI, t0, t0, 0xEF01),
        J(JOP, 2),
    };
    CHECK(cpu.load_program(prog));
    CHECK(cpu.run() == StepResult::Halt);
    CHECK(cpu.regs().read(t0) == 0xABCDEF01u);
    (void)label;
}

static void test_bne_countdown(IProcessor& cpu, std::string_view label) {
    using namespace enc;
    cpu.reset(true);
    std::vector<uint32_t> prog = {
        I(ADDI, zero, t0, 5),
        I(ADDI, zero, t1, 0),
        I(ADDI, t1, t1, 10),
        I(ADDI, t0, t0, static_cast<uint16_t>(-1)),
        I(BNE, t0, zero, static_cast<uint16_t>(-3)),
        J(JOP, 6),
        J(JOP, 6),
    };
    CHECK(cpu.load_program(prog));
    CHECK(cpu.run() == StepResult::Halt);
    CHECK(cpu.regs().read(t1) == 50);
    CHECK(cpu.regs().read(t0) == 0);
    (void)label;
}

static void test_zero_hardwired(IProcessor& cpu, std::string_view label) {
    using namespace enc;
    cpu.reset(true);
    std::vector<uint32_t> prog = {
        I(ADDI, zero, zero, 5),  // write to $zero — must be ignored
        J(JOP, 1),
    };
    CHECK(cpu.load_program(prog));
    CHECK(cpu.run() == StepResult::Halt);
    CHECK(cpu.regs().read(zero) == 0);
    (void)label;
}

static void test_fault_oob(IProcessor& cpu, std::string_view label) {
    using namespace enc;
    // A word with opcode 0x3F has no defined MIPS instruction — Decoder::decode
    // returns nullopt and the CPU raises a Reserved Instruction (RI) exception.
    // Single-cycle raises on step 1 (decode + execute in same step).  Pipelined
    // raises on step 2 (instruction reaches ID stage where decode happens).
    // run() stops on the first non-Ok result, so both implementations converge.
    cpu.reset(true);
    CHECK(cpu.load_program({0xFC00'0000u}));
    CHECK(cpu.run(10) == StepResult::Exception);
    (void)label;
}

// ─── Pipeline-specific: verify PipelineState is populated ─────────────────────
static void test_pipeline_state_single(SingleCycleCpu& cpu) {
    using namespace enc;
    cpu.reset(true);
    cpu.load_program({I(ADDI, zero, t0, 42), J(JOP, 1)});
    cpu.step();
    const PipelineState& ps = cpu.pipeline_state();
    // Single-cycle: only EX populated
    CHECK(ps.stages[2].valid);
    CHECK(!ps.stages[0].valid);
    CHECK(!ps.stages[1].valid);
    CHECK(ps.cycle == 1);
}

static void test_pipeline_state_pipelined(PipelinedCpu& cpu) {
    using namespace enc;
    cpu.reset(true);
    cpu.load_program({I(ADDI, zero, t0, 1), I(ADDI, zero, t1, 2), J(JOP, 2)});

    // After a few cycles the pipelined CPU should show multiple active stages.
    // Run 3 cycles and check the pipeline state is non-trivially populated.
    cpu.step();  // cycle 1: IF sees addi-t0
    cpu.step();  // cycle 2: ID sees addi-t0, IF sees addi-t1
    cpu.step();  // cycle 3: EX sees addi-t0, ID sees addi-t1, IF sees j-self

    const PipelineState& ps = cpu.pipeline_state();
    CHECK(ps.cycle == 3);
    // At cycle 3 the WB and MEM stages should still be empty (no instruction
    // has reached them yet).  At least IF, ID, EX should be valid.
    CHECK(ps.stages[0].valid);  // IF
    CHECK(ps.stages[1].valid);  // ID
    CHECK(ps.stages[2].valid);  // EX
    // cycle_count() == 3
    CHECK(cpu.cycle_count() == 3);
}

// Load-use hazard: LW followed immediately by a dependent ADD.
// The pipelined CPU must stall and still produce the correct result.
static void test_load_use_hazard(IProcessor& cpu, std::string_view label) {
    using namespace enc;
    cpu.reset(true);
    // mem[4096] is pre-loaded with 99 via SW then loaded via LW.
    // ADD immediately uses the loaded value — triggers a load-use stall.
    std::vector<uint32_t> prog = {
        I(ADDI, zero, s0, 4096),  // base = 4096
        I(ADDI, zero, t1, 99),    // t1 = 99
        I(SW, s0, t1, 0),         // mem[4096] = 99
        I(LW, s0, t0, 0),         // t0 = mem[4096] = 99   ← load
        I(ADDI, t0, t2, 1),       // t2 = t0 + 1 = 100     ← uses t0 immediately!
        J(JOP, 5),
    };
    CHECK(cpu.load_program(prog));
    CHECK(cpu.run() == StepResult::Halt);
    CHECK(cpu.regs().read(t0) == 99);
    CHECK(cpu.regs().read(t2) == 100);
    (void)label;
}

// Forwarding: two consecutive dependent ALU instructions.
// Without EX/MEM→EX forwarding the pipelined CPU would produce the wrong value.
static void test_forwarding(IProcessor& cpu, std::string_view label) {
    using namespace enc;
    cpu.reset(true);
    std::vector<uint32_t> prog = {
        I(ADDI, zero, t0, 5),  // t0 = 5
        I(ADDI, t0, t1, 3),    // t1 = t0 + 3 = 8   (EX/MEM→EX forward)
        I(ADDI, t1, t2, 2),    // t2 = t1 + 2 = 10  (EX/MEM→EX forward)
        J(JOP, 3),
    };
    CHECK(cpu.load_program(prog));
    CHECK(cpu.run() == StepResult::Halt);
    CHECK(cpu.regs().read(t1) == 8);
    CHECK(cpu.regs().read(t2) == 10);
    (void)label;
}

// ─── Backward-compat check: `Cpu` alias still works ─────────────────────────
static void test_cpu_alias() {
    using namespace enc;
    mips::Cpu cpu;  // must compile: Cpu = SingleCycleCpu
    cpu.load_program({I(ADDI, zero, t0, 7), J(JOP, 1)});
    CHECK(cpu.run() == StepResult::Halt);
    CHECK(cpu.regs().read(t0) == 7);
}

// ─── Runner ──────────────────────────────────────────────────────────────────
template <typename Fn> static void run_on_both(Fn&& fn, const char* name) {
    SingleCycleCpu sc;
    fn(sc, std::string_view{name} /* + " [SC]" */);
    PipelinedCpu pl;
    fn(pl, std::string_view{name} /* + " [PL]" */);
}

int main() {
    run_on_both(test_arithmetic, "arithmetic");
    run_on_both(test_load_store, "load_store");
    run_on_both(test_branch_loop, "branch_loop");
    run_on_both(test_call_return, "call_return");
    run_on_both(test_lui_ori, "lui_ori");
    run_on_both(test_bne_countdown, "bne_countdown");
    run_on_both(test_zero_hardwired, "zero_hardwired");
    run_on_both(test_fault_oob, "fault_oob");
    run_on_both(test_load_use_hazard, "load_use_hazard");
    run_on_both(test_forwarding, "forwarding");

    // Implementation-specific checks
    {
        SingleCycleCpu sc;
        test_pipeline_state_single(sc);
    }
    {
        PipelinedCpu pl;
        test_pipeline_state_pipelined(pl);
    }

    test_cpu_alias();

    std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return (g_failed > 0) ? 1 : 0;
}
