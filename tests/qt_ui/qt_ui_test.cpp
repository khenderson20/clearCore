#include "nsc_qt/assembler.h"
#include "nsc_qt/simulator_controller.h"
#include "nsc_qt/widgets/memory_widget.h"
#include "nsc_qt/widgets/pipeline_trace_widget.h"
#include "nsc_qt/widgets/register_widget.h"

#include "mips/pipelined_cpu.h"

#include <QApplication>
#include <cassert>
#include <cstdio>
#include <memory>

// ── Minimal test harness ──────────────────────────────────────────────────────

static int g_pass = 0, g_fail = 0;

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (expr) {                                                                                \
            ++g_pass;                                                                              \
        } else {                                                                                   \
            ++g_fail;                                                                              \
            std::fprintf(stderr, "FAIL  %s:%d  %s\n", __FILE__, __LINE__, #expr);                  \
        }                                                                                          \
    } while (false)

// ── Assembler tests ───────────────────────────────────────────────────────────

static void test_assembler_basic() {
    using namespace nsc::qt;

    // nop encodes as 0
    auto r = assemble("nop");
    CHECK(r.ok());
    CHECK(r.words.size() == 1);
    CHECK(r.words[0] == 0x00000000u);

    // add $t0, $t1, $t2
    r = assemble("add $t0, $t1, $t2");
    CHECK(r.ok());
    CHECK(r.words.size() == 1);
    // SPECIAL(0) | rs=$t1(9)<<21 | rt=$t2(10)<<16 | rd=$t0(8)<<11 | shamt=0 | funct=0x20
    const uint32_t expected =
        (0u << 26) | (9u << 21) | (10u << 16) | (8u << 11) | (0u << 6) | 0x20u;
    CHECK(r.words[0] == expected);

    // addi $t0, $zero, 5
    r = assemble("addi $t0, $zero, 5");
    CHECK(r.ok());
    CHECK(r.words.size() == 1);
    // 0x08 | rs=$zero(0)<<21 | rt=$t0(8)<<16 | imm=5
    CHECK(r.words[0] == ((0x08u << 26) | (0u << 21) | (8u << 16) | 5u));

    // lw $t0, 4($sp)
    r = assemble("lw $t0, 4($sp)");
    CHECK(r.ok());
    CHECK(r.words.size() == 1);
    // 0x23 | rs=$sp(29)<<21 | rt=$t0(8)<<16 | imm=4
    CHECK(r.words[0] == ((0x23u << 26) | (29u << 21) | (8u << 16) | 4u));

    // sw $t0, 0($sp)
    r = assemble("sw $t0, 0($sp)");
    CHECK(r.ok());
    CHECK(r.words.size() == 1);
    CHECK(r.words[0] == ((0x2Bu << 26) | (29u << 21) | (8u << 16) | 0u));
}

static void test_assembler_labels() {
    using namespace nsc::qt;

    // Branch to label
    const std::string src = "loop:\n"
                            "  addi $t0, $t0, 1\n"
                            "  beq $t0, $t1, loop\n";
    auto              r   = assemble(src);
    CHECK(r.ok());
    CHECK(r.words.size() == 2);
    // beq at index 1 targets index 0; offset = 0 - (1+1) = -2
    const uint32_t beq_word = r.words[1];
    const int16_t  offset   = static_cast<int16_t>(beq_word & 0xFFFF);
    CHECK(offset == -2);
}

static void test_assembler_errors() {
    using namespace nsc::qt;

    auto r = assemble("add $t0, $t1");  // too few operands
    CHECK(!r.ok());

    r = assemble("add $t0, $t1, $99");  // invalid register
    CHECK(!r.ok());

    r = assemble("fakeinstr $t0");  // unknown mnemonic
    CHECK(!r.ok());

    r = assemble("beq $t0, $t1, missing_label");
    CHECK(!r.ok());
}

// ── SimulatorController signal emission ──────────────────────────────────────

static void test_controller_step_signal() {
    using namespace nsc::qt;

    auto proc = std::make_unique<mips::PipelinedCpu>();
    // Load: addi $t0,$zero,1 (0x20080001)
    CHECK(proc->load_program({0x20080001u}));

    SimulatorController ctrl(std::move(proc));

    uint64_t received_count = 0;
    bool     ps_received    = false;

    QObject::connect(&ctrl, &SimulatorController::cycleExecuted,
                     [&](uint64_t n) { received_count = n; });
    QObject::connect(&ctrl, &SimulatorController::pipelineStateChanged,
                     [&](mips::PipelineState) { ps_received = true; });

    ctrl.stepCycle();
    CHECK(received_count == 1);
    CHECK(ps_received);
    CHECK(ctrl.cycleCount() == 1);
}

static void test_controller_breakpoint() {
    using namespace nsc::qt;

    auto proc = std::make_unique<mips::PipelinedCpu>();
    // Two nops then a halt-loop (j 0x00000002 → self-loop at word 2)
    CHECK(proc->load_program({0u, 0u, 0x08000002u}));

    SimulatorController ctrl(std::move(proc));
    ctrl.setBreakpoint(0x04);  // word 1 (PC = byte address 4)

    bool     bp_hit = false;
    uint32_t bp_pc  = 0;
    QObject::connect(&ctrl, &SimulatorController::breakpointHit, [&](uint32_t pc) {
        bp_hit = true;
        bp_pc  = pc;
    });

    // Step many times until breakpoint fires (or 20 steps max)
    for (int i = 0; i < 20 && !bp_hit; ++i)
        ctrl.stepCycle();

    CHECK(bp_hit);
    CHECK(bp_pc == 0x04u);
}

static void test_controller_reset() {
    using namespace nsc::qt;

    auto proc = std::make_unique<mips::PipelinedCpu>();
    CHECK(proc->load_program({0x20080001u}));  // addi $t0,$zero,1

    SimulatorController ctrl(std::move(proc));
    ctrl.stepCycle();
    CHECK(ctrl.cycleCount() == 1);

    ctrl.reset();
    CHECK(ctrl.cycleCount() == 0);
    CHECK(ctrl.statistics().cycles_executed == 0);
}

// ── RegisterWidget state tracking ────────────────────────────────────────────

static void test_register_widget_clear() {
    using namespace nsc::qt;

    RegisterWidget rw;
    rw.clear();
    // After clear, $t0 (reg 8) should show 0
    CHECK(rw.value(8) == 0u);
}

// ── MemoryWidget navigation ───────────────────────────────────────────────────

static void test_memory_widget_construct() {
    using namespace nsc::qt;

    MemoryWidget mw;
    // Smoke test: widget constructs without crashing
    CHECK(true);
}

// ── PipelineTraceWidget timeline ─────────────────────────────────────────────

static void test_trace_widget_clear() {
    using namespace nsc::qt;

    PipelineTraceWidget tw;
    tw.clear();
    // Smoke test: clear doesn't crash
    CHECK(true);
}

// ── Main ─────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    test_assembler_basic();
    test_assembler_labels();
    test_assembler_errors();
    test_controller_step_signal();
    test_controller_breakpoint();
    test_controller_reset();
    test_register_widget_clear();
    test_memory_widget_construct();
    test_trace_widget_clear();

    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
