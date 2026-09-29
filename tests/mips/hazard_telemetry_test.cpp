// ─── hazard_telemetry_test.cpp ────────────────────────────────────────────────
// Pins the per-cycle hazard indicators in isa::PipelineState (load_stall,
// fwd_*, branch_flush, retired) against small hand-verified programs. The UIs
// accumulate these into CPI / stall / flush telemetry, so a regression here
// silently corrupts what the pipeline visualiser reports.
//
// Every program ends in a self-targeting J (the halt convention).

#include "mips/pipelined_cpu.h"
#include "mips/processor.h"
#include "mips/single_cycle_cpu.h"

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
constexpr uint32_t ADDI = 0x08, LW = 0x23, BEQ = 0x04, JOP = 0x02;
constexpr uint32_t F_ADD = 0x20;
constexpr uint32_t zero = 0, t0 = 8, t1 = 9, t2 = 10, t3 = 11;
}  // namespace enc

struct Telemetry {
    int stalls = 0, forwards = 0, flushes = 0, retired = 0;
};

// Steps to halt, summing the per-cycle indicators. A forward is counted once
// per cycle in which any bypass path is active.
static Telemetry run_counting(isa::IProcessor& cpu, const std::vector<uint32_t>& prog) {
    cpu.reset(true);
    CHECK(cpu.load_program(prog));
    Telemetry t;
    for (int i = 0; i < 200; ++i) {
        const StepResult r = cpu.step();
        const auto&      s = cpu.pipeline_state();
        t.stalls += s.load_stall ? 1 : 0;
        t.forwards +=
            (s.fwd_ex_to_ex_a || s.fwd_ex_to_ex_b || s.fwd_mem_to_ex_a || s.fwd_mem_to_ex_b) ? 1
                                                                                             : 0;
        t.flushes += s.branch_flush ? 1 : 0;
        t.retired += s.retired ? 1 : 0;
        if (r == StepResult::Halt || r == StepResult::Fault) break;
    }
    return t;
}

// Halt J targets its own word address: word index `idx` → target field `idx`.
static uint32_t halt_at(uint32_t idx) {
    return enc::J(enc::JOP, idx);
}

static void test_load_use_stall() {
    using namespace enc;
    // lw t0,0(zero); add t2,t0,t1 — dependent, so exactly one bubble.
    const std::vector<uint32_t> prog = {
        I(LW, zero, t0, 0),
        R(t0, t1, t2, 0, F_ADD),
        halt_at(2),
    };
    PipelinedCpu pl;
    const auto   t = run_counting(pl, prog);
    CHECK(t.stalls == 1);
    CHECK(t.flushes == 0);
}

static void test_no_stall_without_dependency() {
    using namespace enc;
    // lw t0; add t2,t1,t1 — no dependence on the loaded register.
    const std::vector<uint32_t> prog = {
        I(LW, zero, t0, 0),
        R(t1, t1, t2, 0, F_ADD),
        halt_at(2),
    };
    PipelinedCpu pl;
    CHECK(run_counting(pl, prog).stalls == 0);
}

static void test_ex_to_ex_forward() {
    using namespace enc;
    const std::vector<uint32_t> prog = {
        I(ADDI, zero, t0, 5),
        R(t0, t0, t1, 0, F_ADD),  // reads t0 one instruction after it is written
        halt_at(2),
    };
    PipelinedCpu pl;
    const auto   t = run_counting(pl, prog);
    CHECK(t.stalls == 0);
    CHECK(t.forwards == 1);
}

static void test_taken_beq_flush() {
    using namespace enc;
    // beq zero,zero,+2 skips the two ADDIs; both are squashed and never write.
    const std::vector<uint32_t> prog = {
        I(BEQ, zero, zero, 2), I(ADDI, zero, t0, 1), I(ADDI, zero, t1, 2),
        I(ADDI, zero, t2, 3),  halt_at(4),
    };
    PipelinedCpu pl;
    const auto   t = run_counting(pl, prog);
    CHECK(t.flushes == 1);
    CHECK(pl.regs().read(t0) == 0);
    CHECK(pl.regs().read(t1) == 0);
    CHECK(pl.regs().read(t2) == 3);
    CHECK(t.retired == 3);  // beq, the ADDI at the target, and the halt J
}

static void test_jump_flush() {
    using namespace enc;
    // j skips one ADDI. A jump resolves in ID (flush_from_id, 1 stage), and
    // PipelineState::branch_flush only reports flush_from_ex, so a jump's
    // squash is visible in architectural state but NOT in the flush indicator.
    // Pinned here so a future change to that (see #192) is a deliberate one.
    const std::vector<uint32_t> prog = {
        J(JOP, 2),
        I(ADDI, zero, t0, 1),
        I(ADDI, zero, t1, 2),
        halt_at(3),
    };
    PipelinedCpu pl;
    const auto   t = run_counting(pl, prog);
    CHECK(t.flushes == 0);
    CHECK(pl.regs().read(t0) == 0);
    CHECK(pl.regs().read(t1) == 2);
}

// Retired counts are model-independent.
static void test_straight_line(isa::IProcessor& cpu) {
    using namespace enc;
    const std::vector<uint32_t> prog = {
        I(ADDI, zero, t0, 1), I(ADDI, zero, t1, 2), I(ADDI, zero, t2, 3),
        I(ADDI, zero, t3, 4), halt_at(4),
    };
    const auto t = run_counting(cpu, prog);
    CHECK(t.stalls == 0);
    CHECK(t.flushes == 0);
    CHECK(t.retired >= 4);
    CHECK(cpu.regs().read(t3) == 4);
}

int main() {
    test_load_use_stall();
    test_no_stall_without_dependency();
    test_ex_to_ex_forward();
    test_taken_beq_flush();
    test_jump_flush();
    {
        SingleCycleCpu sc;
        test_straight_line(sc);
    }
    {
        PipelinedCpu pl;
        test_straight_line(pl);
    }
    std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return (g_failed > 0) ? 1 : 0;
}
