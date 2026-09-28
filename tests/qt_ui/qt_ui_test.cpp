#include "nsc_qt/assembler.h"
#include "nsc_qt/dock_panels.h"
#include "nsc_qt/simulator_controller.h"
#include "nsc_qt/widgets/memory_widget.h"
#include "nsc_qt/widgets/pipeline_events_widget.h"
#include "nsc_qt/widgets/pipeline_trace_widget.h"
#include "nsc_qt/widgets/register_widget.h"
#include "nsc_qt/widgets/schematic_datapath_widget.h"

#include "mips/pipelined_cpu.h"
#include "mips/single_cycle_cpu.h"

#include <DockAreaWidget.h>
#include <DockManager.h>
#include <DockWidget.h>
#include <QApplication>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QHelpEvent>
#include <QHexView/model/qhexdocument.h>
#include <QHexView/qhexview.h>
#include <QLabel>
#include <QMainWindow>
#include <QString>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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

    // Comment-only / blank source encodes no program and must not report success.
    r = assemble("# just a comment\n\n   \n");
    CHECK(!r.ok());

    // Non-ASCII bytes in a mnemonic must be rejected, not fed to tolower as negatives.
    r = assemble("\xE2\x80\x9Cnop\xE2\x80\x9D");
    CHECK(!r.ok());

    // INT32_MIN spelled as a negated hex literal (would overflow if negated as int32_t).
    // Parsing it must not be UB; the value is then outside lui's range and rejected.
    r = assemble("lui $t0, -0x80000000");
    CHECK(!r.ok());
}

// Every operand field has a fixed width; an out-of-range value must be an error, not
// silently truncated into a different program (#233).
static void test_assembler_operand_ranges() {
    using namespace nsc::qt;
    const auto low16 = [](const AssemblerResult& r) { return r.words.at(0) & 0xFFFFu; };

    // Sign-extended 16-bit immediate: addi addiu slti sltiu
    auto r = assemble("addi $t0, $zero, 32767");
    CHECK(r.ok() && low16(r) == 0x7FFFu);
    r = assemble("addi $t0, $zero, -32768");
    CHECK(r.ok() && low16(r) == 0x8000u);
    CHECK(!assemble("addi $t0, $zero, 32768").ok());
    CHECK(!assemble("addi $t0, $zero, -32769").ok());
    CHECK(!assemble("addi $t0, $zero, 70000").ok());  // used to encode 4464
    CHECK(!assemble("sltiu $t0, $zero, 0xFFFF").ok());

    // Zero-extended 16-bit immediate: andi ori xori lui
    r = assemble("ori $t0, $zero, 65535");
    CHECK(r.ok() && low16(r) == 0xFFFFu);
    CHECK(!assemble("ori $t0, $zero, 65536").ok());
    CHECK(!assemble("andi $t0, $t0, -1").ok());
    r = assemble("lui $t0, 0xFFFF");
    CHECK(r.ok() && low16(r) == 0xFFFFu);
    CHECK(!assemble("lui $t0, 0x10000").ok());

    // Memory offsets are sign-extended 16-bit
    r = assemble("lw $t0, -32768($sp)");
    CHECK(r.ok() && low16(r) == 0x8000u);
    CHECK(!assemble("lw $t0, 32768($sp)").ok());
    CHECK(!assemble("sw $t0, -32769($sp)").ok());

    // Branch displacements are sign-extended 16-bit word offsets
    r = assemble("beq $zero, $zero, 32767");
    CHECK(r.ok() && low16(r) == 0x7FFFu);
    CHECK(!assemble("bne $t0, $t1, 32768").ok());
    CHECK(!assemble("beq $t0, $t1, -32769").ok());

    // A label exactly 32767 instructions ahead fits; one more does not. The branch is
    // at index 0 and the label follows `nops` nops, so the offset is `nops` itself.
    const auto branch_over = [](int nops) {
        std::string src = "beq $zero, $zero, far\n";
        for (int i = 0; i < nops; ++i)
            src += "nop\n";
        return assemble(src + "far: nop\n");
    };
    r = branch_over(32767);
    CHECK(r.ok() && low16(r) == 0x7FFFu);
    r = branch_over(32768);
    CHECK(!r.ok());
    CHECK(r.error.value_or("").rfind("line 1:", 0) == 0);  // reported on the branch's line

    // Jump targets are 26-bit word indexes
    r = assemble("j 0x3FFFFFF");
    CHECK(r.ok() && (r.words.at(0) & 0x03FFFFFFu) == 0x03FFFFFFu);
    CHECK(!assemble("j 0x4000000").ok());
    CHECK(!assemble("jal -1").ok());
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
                     [&](const mips::PipelineState&) { ps_received = true; });

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

static std::unique_ptr<mips::IProcessor> make_cpu(bool pipelined) {
    if (pipelined) return std::make_unique<mips::PipelinedCpu>();
    return std::make_unique<mips::SingleCycleCpu>();
}

// Steps until the program halts; false if it has not halted in `max_steps`.
static bool step_until_halt(nsc::qt::SimulatorController& ctrl, int max_steps = 100) {
    bool       halted = false;
    const auto conn   = QObject::connect(&ctrl, &nsc::qt::SimulatorController::halted,
                                         [&halted] { halted = true; });
    for (int i = 0; i < max_steps && !halted; ++i)
        ctrl.stepCycle();
    QObject::disconnect(conn);
    return halted;
}

// reset() returns to the state right after loadProgram(), memory included, so
// a run's stores cannot change the next run. They used to survive: a counter
// in memory gave 2 on the second run, and a store over the program's first
// word made the rerun trap with RI (#292).
static void test_controller_reset_restores_memory() {
    using namespace nsc::qt;

    // lw $t0,0x100($zero) ; addi $t0,$t0,1 ; sw $t0,0x100($zero) ; halt: j halt
    const std::vector<uint32_t> counter = {0x8C080100u, 0x21080001u, 0xAC080100u, 0x08000003u};
    // addi $t0,$zero,7 ; sw $t0,0($zero) ; lw $t1,0($zero) ; add $t2,$t1,$t1 ; halt: j halt
    const std::vector<uint32_t> self_store = {0x20080007u, 0xAC080000u, 0x8C090000u, 0x01295020u,
                                              0x08000004u};

    for (const bool pipelined : {false, true}) {
        SimulatorController ctrl(make_cpu(pipelined));
        bool                trapped = false;
        QObject::connect(&ctrl, &SimulatorController::exceptionRaised,
                         [&trapped] { trapped = true; });

        CHECK(ctrl.loadProgram(counter));
        CHECK(step_until_halt(ctrl));
        CHECK(ctrl.registerValue(8) == 1u);  // $t0
        ctrl.reset();
        CHECK(ctrl.cycleCount() == 0);
        CHECK(ctrl.memoryWord(0x100) == 0u);      // the store is gone
        CHECK(ctrl.memoryWord(0) == counter[0]);  // the program is back
        CHECK(step_until_halt(ctrl));
        CHECK(ctrl.registerValue(8) == 1u);

        CHECK(ctrl.loadProgram(self_store));
        CHECK(step_until_halt(ctrl));
        CHECK(ctrl.memoryWord(0) == 7u);  // the store replaced the first instruction
        ctrl.reset();
        CHECK(ctrl.memoryWord(0) == self_store[0]);
        CHECK(step_until_halt(ctrl));
        CHECK(ctrl.registerValue(10) == 14u);  // $t2 = 7 + 7
        CHECK(!trapped);
    }
}

// A load starts from an empty machine: the tail of a longer earlier program and
// the stores of an earlier run are gone. A program that does not fit leaves the
// machine empty, and reset() then does not bring the earlier program back.
static void test_controller_load_starts_empty() {
    using namespace nsc::qt;

    SimulatorController ctrl(std::make_unique<mips::PipelinedCpu>());  // 64 KiB
    // lw $t0,0x100($zero) ; addi $t0,$t0,1 ; sw $t0,0x100($zero) ; halt: j halt
    CHECK(ctrl.loadProgram({0x8C080100u, 0x21080001u, 0xAC080100u, 0x08000003u}));
    CHECK(step_until_halt(ctrl));
    CHECK(ctrl.memoryWord(0x100) == 1u);

    CHECK(ctrl.loadProgram({0x08000000u}));  // halt: j halt
    CHECK(ctrl.cycleCount() == 0);
    CHECK(ctrl.memoryWord(4) == 0u);
    CHECK(ctrl.memoryWord(0x100) == 0u);

    const std::vector<uint32_t> too_big(((64u << 10) / 4) + 1, 0x08000000u);
    CHECK(!ctrl.loadProgram(too_big));
    CHECK(ctrl.memoryWord(0) == 0u);
    ctrl.reset();
    CHECK(ctrl.memoryWord(0) == 0u);
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

// The view shows the widget's own snapshot and refreshes it incrementally: only
// changed bytes are copied, and exactly those are highlighted (#241, #245).
static void test_memory_widget_incremental_refresh() {
    using namespace nsc::qt;

    MemoryWidget mw;
    isa::Memory  mem(1u << 14);  // 16 KiB: four 4 KiB comparison pages
    CHECK(mem.write_word(0x100, 0x1111'1111u));

    mw.updateDisplay(mem);  // first refresh: full copy, nothing highlighted
    CHECK(mw.changedRanges().empty());
    auto* view = mw.findChild<QHexView*>();
    CHECK(view != nullptr && view->hexDocument() != nullptr);
    CHECK(view->hexDocument()->length() == static_cast<qint64>(mem.size()));
    CHECK(view->hexDocument()->read(0x100, 4) == QByteArray("\x11\x11\x11\x11", 4));

    mw.updateDisplay(mem);  // nothing changed
    CHECK(mw.changedRanges().empty());

    CHECK(mem.write_byte(0x20, 0xAB));
    CHECK(mem.write_word(0x2000, 0xDEAD'BEEFu));
    // A run that crosses the 4 KiB page boundary is still one range.
    CHECK(mem.write_word(0x0FFE & ~3u, 0xFFFF'FFFFu));
    CHECK(mem.write_word(0x1000, 0xFFFF'FFFFu));
    mw.updateDisplay(mem);
    const auto& r = mw.changedRanges();
    CHECK(r.size() == 3);
    CHECK((r.size() == 3 && r[0] == std::pair<qint64, qint64>(0x20, 1)));
    CHECK((r.size() == 3 && r[1] == std::pair<qint64, qint64>(0x0FFC, 8)));
    CHECK((r.size() == 3 && r[2] == std::pair<qint64, qint64>(0x2000, 4)));
    CHECK(view->hexDocument()->read(0x2000, 4) == QByteArray("\xEF\xBE\xAD\xDE", 4));
    CHECK(view->hexDocument()->read(0x20, 1) == QByteArray("\xAB", 1));

    // The document is patched in place, not replaced (which reset the cursor).
    const QHexDocument* doc = view->hexDocument();
    mw.updateDisplay(mem);
    CHECK(view->hexDocument() == doc);
    CHECK(mw.changedRanges().empty());  // highlights last one refresh

    mw.setDarkMode(true);  // must not need the simulator's memory
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

// Text of the trace cell for (row, column); empty when there is no item.
static QString trace_cell(QTableWidget* t, int row, int col) {
    const auto* item = t->item(row, col);
    return item ? item->text() : QString();
}

// Columns are real cycles (#238): a snapshot of cycle 5 lands under header "5",
// not in the first column, and a later cycle leaves the skipped ones blank.
static void test_trace_widget_columns_follow_state_cycle() {
    using namespace nsc::qt;

    PipelineTraceWidget tw;
    tw.show();
    auto* table = tw.findChild<QTableWidget*>();
    CHECK(table != nullptr);

    mips::PipelineState ps;
    ps.cycle     = 5;
    ps.stages[0] = {"IF", true, false, false, 0x40, 0x2108'0001u};
    tw.updateCycle(ps);
    CHECK(table->rowCount() == 1);
    CHECK(table->horizontalHeaderItem(5)->text() == QStringLiteral("5"));
    CHECK(trace_cell(table, 0, 5) == QStringLiteral("IF"));
    CHECK(trace_cell(table, 0, 1).isEmpty());

    ps.cycle     = 7;  // cycle 6 was never reported
    ps.stages[0] = {};
    ps.stages[1] = {"ID", true, false, false, 0x40, 0x2108'0001u};
    tw.updateCycle(ps);
    CHECK(table->rowCount() == 1);
    CHECK(trace_cell(table, 0, 6).isEmpty());
    CHECK(trace_cell(table, 0, 7) == QStringLiteral("ID"));

    // A jump past the whole window starts a fresh diagram.
    ps.cycle = 5000;
    tw.updateCycle(ps);
    CHECK(table->rowCount() == 1);
    CHECK(table->horizontalHeaderItem(20)->text() == QStringLiteral("5000"));
    CHECK(trace_cell(table, 0, 20) == QStringLiteral("ID"));
}

// Rows are dynamic instances (#239): in a 2-instruction loop (period 4, shorter
// than the pipeline) one PC is in WB and IF in the same cycle, and both must
// be shown, on different rows.
static void test_trace_widget_rows_are_dynamic_instances() {
    using namespace nsc::qt;

    PipelineTraceWidget tw;
    tw.show();
    auto* table = tw.findChild<QTableWidget*>();

    mips::PipelinedCpu cpu;
    // 0x0: addi $t0, $t0, 1   0x4: beq $zero, $zero, -2 (back to 0x0)
    CHECK(cpu.load_program({0x2108'0001u, 0x1000'FFFEu}));
    for (int i = 0; i < 16; ++i) {
        (void)cpu.step();
        tw.updateCycle(cpu.pipeline_state());
    }

    int  rows_for_pc0     = 0;
    bool same_cycle_twice = false;
    int  cells_in_one_row = 0;
    for (int r = 0; r < table->rowCount(); ++r) {
        if (!trace_cell(table, r, 0).startsWith(QStringLiteral("0x0000 "))) continue;
        ++rows_for_pc0;
        int filled = 0;
        for (int c = 1; c < table->columnCount(); ++c)
            filled += trace_cell(table, r, c).isEmpty() ? 0 : 1;
        cells_in_one_row = std::max(cells_in_one_row, filled);
        for (int r2 = r + 1; r2 < table->rowCount(); ++r2) {
            if (!trace_cell(table, r2, 0).startsWith(QStringLiteral("0x0000 "))) continue;
            for (int c = 1; c < table->columnCount(); ++c)
                if (!trace_cell(table, r, c).isEmpty() && !trace_cell(table, r2, c).isEmpty())
                    same_cycle_twice = true;
        }
    }
    CHECK(rows_for_pc0 >= 3);      // one row per execution of the addi
    CHECK(same_cycle_twice);       // WB of one instance and IF of the next, same column
    CHECK(cells_in_one_row == 5);  // an instance keeps its whole IF..WB history
}

// A hidden trace keeps recording but only redraws when shown (#242).
static void test_trace_widget_defers_redraw_while_hidden() {
    using namespace nsc::qt;

    PipelineTraceWidget tw;  // never shown yet
    auto*               table = tw.findChild<QTableWidget*>();
    mips::PipelineState ps;
    ps.cycle     = 1;
    ps.stages[0] = {"IF", true, false, false, 0x0, 0};
    tw.updateCycle(ps);
    CHECK(table->rowCount() == 0);  // recorded, not drawn
    tw.show();
    CHECK(table->rowCount() == 1);
    CHECK(trace_cell(table, 0, 1) == QStringLiteral("IF"));
}

// ── PipelineEventsWidget log behaviour ───────────────────────────────────────

static void test_events_widget_log() {
    using namespace nsc::qt;

    PipelineEventsWidget ew;
    CHECK(ew.eventCount() == 0);

    ew.logEvent(PipelineEventsWidget::Kind::Info, 0, "program loaded");
    CHECK(ew.eventCount() == 1);

    // A forwarding event is derived from the pipeline state.
    mips::PipelineState st{};
    st.stages[2]      = {"EX", true, false, false, 0x08, 0x012A5020u};  // add $t2,$t1,$t2
    st.fwd_ex_to_ex_a = true;
    st.cycle          = 4;
    ew.updateCycle(st);
    CHECK(ew.eventCount() == 2);

    // The same event on the very next cycle is suppressed (rising edge only)…
    st.cycle = 5;
    ew.updateCycle(st);
    CHECK(ew.eventCount() == 2);

    // …but logs again after a gap.
    st.cycle = 9;
    ew.updateCycle(st);
    CHECK(ew.eventCount() == 3);

    // The same kind of event for a different instruction is a new event.
    st.stages[2].raw = 0x012B'5020u;  // add $t2,$t1,$t3
    st.cycle         = 10;
    ew.updateCycle(st);
    CHECK(ew.eventCount() == 4);

    ew.clear();
    CHECK(ew.eventCount() == 0);
}

// ── Dock panels ───────────────────────────────────────────────────────────────

// Regression guard: every panel added via addDockPanel() must actually carry
// its content widget. A dropped setWidget() once shipped a GUI where all panels
// rendered empty (the content widgets were orphaned in the QMainWindow), and
// nothing caught it because the smoke test only checked that the app launched.
static void test_dock_panels_carry_content() {
    using namespace nsc::qt;

    auto  main_window = std::make_unique<QMainWindow>();
    auto* manager     = new ads::CDockManager(main_window.get());

    ads::CDockAreaWidget* area  = nullptr;
    auto*                 first = new QLabel("first");
    auto*                 tab   = new QLabel("tab");

    auto* dock_a = addDockPanel(manager, area, "First", first);
    auto* dock_b = addDockPanel(manager, area, "Tabbed", tab);

    // The content must be inside the dock, not orphaned.
    CHECK(dock_a->widget() == first);
    CHECK(dock_b->widget() == tab);
    // First call opens a central area; the second tabs into the same one.
    CHECK(area != nullptr);
    CHECK(manager->dockWidgetsMap().size() == 2);
}

// ── Hidden panels (#242, #243) ────────────────────────────────────────────────

static bool has_label_text(const QWidget& w, const QString& text) {
    const auto labels = w.findChildren<QLabel*>();
    return std::any_of(labels.begin(), labels.end(),
                       [&](const QLabel* l) { return l->text() == text; });
}

static bool scene_has_text(const QGraphicsView& v, const QString& text) {
    const auto items = v.scene()->items();
    return std::any_of(items.begin(), items.end(), [&](QGraphicsItem* it) {
        const auto* t = qgraphicsitem_cast<QGraphicsSimpleTextItem*>(it);
        return t != nullptr && t->text() == text;
    });
}

static bool scene_has_tooltip(const QGraphicsView& v, const QString& part) {
    const auto items = v.scene()->items();
    return std::any_of(items.begin(), items.end(),
                       [&](QGraphicsItem* it) { return it->toolTip().contains(part); });
}

// QADS hides every tab that is not in front, so the per-cycle updates skip
// those panels; each one catches up when its tab comes to the front.
static void test_hidden_tab_panels_catch_up_when_shown() {
    using namespace nsc::qt;

    auto  main_window = std::make_unique<QMainWindow>();
    auto* manager     = new ads::CDockManager(main_window.get());

    ads::CDockAreaWidget* area     = nullptr;
    auto*                 regs     = new RegisterWidget;
    auto*                 mem      = new MemoryWidget;
    auto*                 dp       = new SchematicDatapathWidget;
    auto*                 reg_dock = addDockPanel(manager, area, "Registers", regs);
    auto*                 mem_dock = addDockPanel(manager, area, "Memory", mem);
    auto*                 dp_dock  = addDockPanel(manager, area, "Datapath", dp);

    int mem_shown = 0;
    QObject::connect(mem, &MemoryWidget::shown, [&] { ++mem_shown; });

    dp_dock->setAsCurrentTab();
    main_window->show();
    CHECK(dp->isVisible());
    CHECK(!regs->isVisible());  // background tabs are hidden, not just covered
    CHECK(!mem->isVisible());
    CHECK(mem_shown == 0);

    // WB: addi $t0,$zero,42 writes $t0; ID: add $t2,$t0,$t1 reads $t0 and $t1.
    mips::PipelineState st{};
    st.stages[1] = {"ID", true, false, false, 0x0C, 0x0109'5020u};
    st.stages[4] = {"WB", true, false, false, 0x00, 0x2008'002Au};
    st.cycle     = 5;
    std::array<uint32_t, 32> vals{};
    vals[8] = 42;

    // The register panel stores the values but does not redraw while hidden.
    regs->updateCycle(st, vals);
    CHECK(regs->value(8) == 42u);
    CHECK(!has_label_text(*regs, QStringLiteral("0x0000002a")));
    reg_dock->setAsCurrentTab();
    CHECK(regs->isVisible());
    CHECK(has_label_text(*regs, QStringLiteral("0x0000002a")));

    // The memory panel asks for a refresh when it comes to the front.
    mem_dock->setAsCurrentTab();
    CHECK(mem->isVisible());
    CHECK(mem_shown == 1);

    // The datapath defers the whole scene while hidden…
    CHECK(!dp->isVisible());
    dp->setCycleState(st, vals);
    const QString wb_label = QStringLiteral("$t0 = 0x0000002a");
    CHECK(!scene_has_text(*dp, wb_label));
    dp_dock->setAsCurrentTab();
    CHECK(scene_has_text(*dp, wb_label));

    // …and builds its tooltips only when one is about to be shown.
    const QString id_reads = QStringLiteral("$t0 = 0x0000002a");
    CHECK(!scene_has_tooltip(*dp, id_reads));
    QHelpEvent tip(QEvent::ToolTip, QPoint(1, 1), dp->viewport()->mapToGlobal(QPoint(1, 1)));
    QApplication::sendEvent(dp->viewport(), &tip);
    CHECK(scene_has_tooltip(*dp, id_reads));
}

// ── Statistics / exception reporting ─────────────────────────────────────────

// The single-cycle model never fills the WB snapshot, so counting retired
// instructions from that slot showed "Instructions: 0, CPI 0.0" in both GUIs.
static void test_controller_single_cycle_stats() {
    using namespace nsc::qt;

    auto proc = std::make_unique<mips::SingleCycleCpu>();
    // addi $t0,$zero,1 ; addi $t1,$zero,2 ; j 2 (self-loop halt)
    CHECK(proc->load_program({0x20080001u, 0x20090002u, 0x08000002u}));

    SimulatorController ctrl(std::move(proc));
    ctrl.stepCycle();
    ctrl.stepCycle();
    ctrl.stepCycle();

    const SimulatorStatistics st = ctrl.statistics();
    CHECK(st.cycles_executed == 3);
    CHECK(st.instructions_retired == 3);
    CHECK(st.cpi() == 1.0);
}

static void test_controller_exception_signal() {
    using namespace nsc::qt;

    auto proc = std::make_unique<mips::PipelinedCpu>();
    // addi $t0,$zero,1 ; syscall ; j 2
    CHECK(proc->load_program({0x20080001u, 0x0000000Cu, 0x08000002u}));

    SimulatorController ctrl(std::move(proc));

    bool     fired = false;
    uint32_t epc   = 0;
    QString  name;
    QObject::connect(&ctrl, &SimulatorController::exceptionRaised,
                     [&](uint32_t e, const QString& n) {
                         fired = true;
                         epc   = e;
                         name  = n;
                     });

    for (int i = 0; i < 10 && !fired; ++i)
        ctrl.stepCycle();

    CHECK(fired);
    CHECK(epc == 4u);
    CHECK(name == QStringLiteral("Sys"));
    CHECK(!ctrl.isRunning());
}

// ── Main ─────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    test_assembler_basic();
    test_assembler_labels();
    test_assembler_errors();
    test_assembler_operand_ranges();
    test_controller_step_signal();
    test_controller_breakpoint();
    test_controller_reset();
    test_controller_reset_restores_memory();
    test_controller_load_starts_empty();
    test_controller_single_cycle_stats();
    test_controller_exception_signal();
    test_register_widget_clear();
    test_memory_widget_construct();
    test_memory_widget_incremental_refresh();
    test_trace_widget_clear();
    test_trace_widget_columns_follow_state_cycle();
    test_trace_widget_rows_are_dynamic_instances();
    test_trace_widget_defers_redraw_while_hidden();
    test_events_widget_log();
    test_dock_panels_carry_content();
    test_hidden_tab_panels_catch_up_when_shown();

    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
