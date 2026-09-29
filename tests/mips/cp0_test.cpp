// CP0 and exception integration tests.
//
// Tests the three layers together:
//   1. Cp0 register semantics (raise / eret / read / write / reset)
//   2. SingleCycleCpu exception paths (SYSCALL, BREAK, overflow, AdEL, RI)
//   3. PipelinedCpu exception paths (SYSCALL, BREAK, overflow, AdEL)

#include "mips/cp0.h"
#include "mips/pipelined_cpu.h"
#include "mips/single_cycle_cpu.h"

#include <cassert>
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

// ─── Instruction encoders (reused from cpu_test.cpp style) ───────────────────
namespace enc {
constexpr uint32_t R(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shamt, uint32_t funct) {
    return (rs << 21) | (rt << 16) | (rd << 11) | (shamt << 6) | funct;
}
constexpr uint32_t I(uint32_t op, uint32_t rs, uint32_t rt, uint16_t imm) {
    return (op << 26) | (rs << 21) | (rt << 16) | imm;
}
// These are complete MIPS encoding tables kept whole for readability; not every
// entry is used by every test, which -Wunused-const-variable flags on Clang.
// Registers
[[maybe_unused]] constexpr uint32_t zero = 0, t0 = 8, t1 = 9, t2 = 10;
// Opcodes
[[maybe_unused]] constexpr uint32_t ADDI = 0x08, LW = 0x23, SW = 0x2B;
// Funct codes
[[maybe_unused]] constexpr uint32_t F_ADD = 0x20, F_SUB = 0x22;
constexpr uint32_t                  F_SYSCALL = 0x0C, F_BREAK = 0x0D;
// SYSCALL: opcode=SPECIAL(0), all fields 0, funct=0x0C
constexpr uint32_t SYSCALL = F_SYSCALL;
// BREAK: same pattern with funct=0x0D
constexpr uint32_t BREAK_INSTR = F_BREAK;
// Halt: J self (J target=0, pc+4[31:28]=0 → stays at 0)
constexpr uint32_t HALT_AT_0 = (0x02u << 26) | 0x0u;
// COP0 funct codes
constexpr uint32_t COP0_OP = 0x10u;
constexpr uint32_t MFC0_RS = 0x00u;
constexpr uint32_t MTC0_RS = 0x04u;
constexpr uint32_t ERET_RS = 0x10u, ERET_FUNCT = 0x18u;

// MFC0 rt, rd — move from CP0 register rd to GPR rt
constexpr uint32_t MFC0(uint32_t rt, uint32_t cp0_reg) {
    return (COP0_OP << 26) | (MFC0_RS << 21) | (rt << 16) | (cp0_reg << 11);
}
// MTC0 rt, rd — move from GPR rt to CP0 register rd
constexpr uint32_t MTC0(uint32_t rt, uint32_t cp0_reg) {
    return (COP0_OP << 26) | (MTC0_RS << 21) | (rt << 16) | (cp0_reg << 11);
}
// ERET
constexpr uint32_t ERET = (COP0_OP << 26) | (ERET_RS << 21) | ERET_FUNCT;
}  // namespace enc

// ─── Cp0 unit tests ───────────────────────────────────────────────────────────

static void test_cp0_raise_eret() {
    mips::Cp0 cp0;
    CHECK(!cp0.in_exception());

    const uint32_t vec = cp0.raise(mips::ExceptionCode::Sys, 0x1000);
    CHECK(vec == mips::kExceptionVector);
    CHECK(cp0.in_exception());
    CHECK(cp0.epc() == 0x1000);
    CHECK(cp0.last_exception() == mips::ExceptionCode::Sys);
    // Cause.ExcCode = 8 (Sys) << 2
    CHECK((cp0.cause() & 0x7Cu) == (8u << 2));

    const uint32_t ret = cp0.eret();
    CHECK(ret == 0x1000);
    CHECK(!cp0.in_exception());
}

static void test_cp0_badvaddr() {
    mips::Cp0 cp0;
    cp0.raise(mips::ExceptionCode::AdEL, 0x2000, 0xDEAD'BEEF);
    CHECK(cp0.bad_vaddr() == 0xDEAD'BEEFu);
}

static void test_cp0_read_write() {
    mips::Cp0 cp0;
    cp0.write(mips::Cp0::kRegStatus, 0xABCD);
    CHECK(cp0.read(mips::Cp0::kRegStatus) == 0xABCDu);
    // Unknown register: read 0, write ignored.
    CHECK(cp0.read(99) == 0);
    cp0.write(99, 0xFF);
    CHECK(cp0.read(99) == 0);
}

static void test_cp0_reset() {
    mips::Cp0 cp0;
    cp0.raise(mips::ExceptionCode::Ov, 0x500);
    cp0.reset();
    CHECK(!cp0.in_exception());
    CHECK(cp0.epc() == 0);
    CHECK(cp0.cause() == 0);
}

// ─── SingleCycleCpu exception tests ──────────────────────────────────────────

static void test_single_syscall() {
    mips::SingleCycleCpu cpu(1u << 16);
    cpu.load_program({enc::SYSCALL, enc::HALT_AT_0}, 0);
    const auto r = cpu.step();
    CHECK(r == mips::StepResult::Exception);
    CHECK(cpu.cp0().last_exception() == mips::ExceptionCode::Sys);
    CHECK(cpu.cp0().epc() == 0x0000);  // PC of the SYSCALL instruction
    CHECK(cpu.cp0().in_exception());
}

static void test_single_break() {
    mips::SingleCycleCpu cpu(1u << 16);
    cpu.load_program({enc::BREAK_INSTR, enc::HALT_AT_0}, 0);
    const auto r = cpu.step();
    CHECK(r == mips::StepResult::Exception);
    CHECK(cpu.cp0().last_exception() == mips::ExceptionCode::Bp);
}

static void test_single_overflow_add() {
    // ADD $t2, $t0, $t1 where t0=0x7FFFFFFF and t1=1 → signed overflow
    mips::SingleCycleCpu cpu(1u << 16);
    cpu.regs().write(enc::t0, 0x7FFF'FFFF);
    cpu.regs().write(enc::t1, 1);
    const uint32_t add_instr = enc::R(enc::t0, enc::t1, enc::t2, 0, enc::F_ADD);
    cpu.load_program({add_instr, enc::HALT_AT_0}, 0);
    const auto r = cpu.step();
    CHECK(r == mips::StepResult::Exception);
    CHECK(cpu.cp0().last_exception() == mips::ExceptionCode::Ov);
    // $t2 must NOT be written on overflow.
    CHECK(cpu.regs().read(enc::t2) == 0);
}

static void test_single_addi_overflow() {
    // ADDI $t0, $zero, 0x7FFF → no overflow (fits).
    // ADDI $t0, $t0, 1 → t0 = 0x8000, still no overflow in 32-bit (0x7FFF+1=0x8000 unsigned OK
    // but signed result is +32768 which fits in int32 so no overflow).
    // Let's test ADDI $t0, $zero, 32767 then ADDI $t0, $t0, 1: 32768 fits; no overflow.
    // Real overflow: $t0 = INT32_MAX, ADDI $t0, $t0, 1.
    mips::SingleCycleCpu cpu(1u << 16);
    cpu.regs().write(enc::t0, 0x7FFF'FFFFu);
    // ADDI t1, t0, 1
    const uint32_t addi = enc::I(enc::ADDI, enc::t0, enc::t1, 1);
    cpu.load_program({addi, enc::HALT_AT_0}, 0);
    const auto r = cpu.step();
    CHECK(r == mips::StepResult::Exception);
    CHECK(cpu.cp0().last_exception() == mips::ExceptionCode::Ov);
}

static void test_single_addr_error_load() {
    // LW from address 0xFFFF (out of 64KB memory)
    mips::SingleCycleCpu cpu(1u << 16);
    // LW $t0, 0($t1) where $t1 = 0xFFFF (misaligned or OOB)
    cpu.regs().write(enc::t1, 0xFFFC);  // points to last word — actually in bounds
    // Use an address definitely out of bounds: 0x10000
    cpu.regs().write(enc::t1, 0x1'0000);
    const uint32_t lw = enc::I(enc::LW, enc::t1, enc::t0, 0);
    cpu.load_program({lw, enc::HALT_AT_0}, 0);
    const auto r = cpu.step();
    CHECK(r == mips::StepResult::Exception);
    CHECK(cpu.cp0().last_exception() == mips::ExceptionCode::AdEL);
    CHECK(cpu.cp0().bad_vaddr() == 0x1'0000u);
}

static void test_single_mfc0_mtc0() {
    mips::SingleCycleCpu cpu(1u << 16);
    // MTC0 $t0, Status — set Status via MTC0
    // MFC0 $t1, Status — read it back into $t1
    cpu.regs().write(enc::t0, 0x1234'5678);
    const std::vector<uint32_t> prog = {
        enc::MTC0(enc::t0, mips::Cp0::kRegStatus),  // CP0.Status = t0
        enc::MFC0(enc::t1, mips::Cp0::kRegStatus),  // t1 = CP0.Status
        enc::HALT_AT_0,
    };
    cpu.load_program(prog, 0);
    cpu.step();  // MTC0
    cpu.step();  // MFC0
    CHECK(cpu.regs().read(enc::t1) == 0x1234'5678u);
}

static void test_single_eret() {
    mips::SingleCycleCpu cpu(1u << 16);
    // Raise an exception to set EPC, then ERET to return.
    cpu.load_program({enc::SYSCALL, enc::ERET, enc::HALT_AT_0}, 0);
    cpu.step();  // SYSCALL — raises Sys exception, PC → exception vector
    // EPC should be 0x0000 (address of SYSCALL)
    CHECK(cpu.cp0().epc() == 0x0000u);
    // Manually set PC back to the ERET instruction (next instruction after SYSCALL).
    // (Normally the exception handler would do this; we shortcut for the test.)
    cpu.set_pc(4);  // address of ERET
    cpu.step();     // ERET — PC ← EPC = 0
    CHECK(!cpu.cp0().in_exception());
    CHECK(cpu.pc() == 0x0000u);
}

// ─── PipelinedCpu exception tests ────────────────────────────────────────────

static void test_pipelined_syscall() {
    mips::PipelinedCpu cpu(1u << 16);
    // SYSCALL at 0, then a few NOPs (SLL $zero, $zero, 0 = 0x00000000).
    const uint32_t NOP = 0x00000000u;
    cpu.load_program({enc::SYSCALL, NOP, NOP, NOP, enc::HALT_AT_0}, 0);
    // Step until Exception (may take a few cycles to propagate through the pipeline).
    mips::StepResult r = mips::StepResult::Ok;
    for (int i = 0; i < 10 && r == mips::StepResult::Ok; ++i)
        r = cpu.step();
    CHECK(r == mips::StepResult::Exception);
    CHECK(cpu.cp0().last_exception() == mips::ExceptionCode::Sys);
}

static void test_pipelined_break() {
    mips::PipelinedCpu cpu(1u << 16);
    const uint32_t     NOP = 0x00000000u;
    cpu.load_program({enc::BREAK_INSTR, NOP, NOP, NOP, enc::HALT_AT_0}, 0);
    mips::StepResult r = mips::StepResult::Ok;
    for (int i = 0; i < 10 && r == mips::StepResult::Ok; ++i)
        r = cpu.step();
    CHECK(r == mips::StepResult::Exception);
    CHECK(cpu.cp0().last_exception() == mips::ExceptionCode::Bp);
}

// ─── Nested exceptions / EPC preservation ────────────────────────────────────

static void test_cp0_nested_raise_keeps_epc() {
    mips::Cp0 cp0;
    cp0.raise(mips::ExceptionCode::Sys, 0x1000);
    // A second exception while EXL=1 (e.g. the vector itself is unmapped)
    // must not overwrite EPC; Cause and BadVAddr still reflect the new trap.
    cp0.raise(mips::ExceptionCode::AdEL, mips::kExceptionVector, mips::kExceptionVector);
    CHECK(cp0.epc() == 0x1000u);
    CHECK(cp0.last_exception() == mips::ExceptionCode::AdEL);
    CHECK((cp0.cause() & 0x7Cu) == (4u << 2));
    CHECK(cp0.bad_vaddr() == mips::kExceptionVector);
    // Once ERET clears EXL the next raise writes EPC again.
    cp0.eret();
    cp0.raise(mips::ExceptionCode::Ov, 0x2000);
    CHECK(cp0.epc() == 0x2000u);
}

// With the default 64 KiB address space the vector at 0x8000_0180 is unmapped,
// so the step after a trap raises AdEL on the fetch. The original EPC must
// survive that, or every front end would blame the vector instead of the
// instruction that actually trapped.
static void test_single_epc_survives_unmapped_vector() {
    mips::SingleCycleCpu cpu(1u << 16);
    cpu.load_program({enc::I(enc::ADDI, enc::zero, enc::t0, 1), enc::SYSCALL, enc::HALT_AT_0}, 0);
    CHECK(cpu.step() == mips::StepResult::Ok);
    CHECK(cpu.step() == mips::StepResult::Exception);
    CHECK(cpu.cp0().epc() == 4u);
    CHECK(cpu.pc() == mips::kExceptionVector);
    CHECK(cpu.step() == mips::StepResult::Exception);  // fetch from the vector fails
    CHECK(cpu.cp0().last_exception() == mips::ExceptionCode::AdEL);
    CHECK(cpu.cp0().epc() == 4u);
}

static void test_pipelined_epc_survives_unmapped_vector() {
    mips::PipelinedCpu cpu(1u << 16);
    cpu.load_program({enc::I(enc::ADDI, enc::zero, enc::t0, 1), enc::SYSCALL, enc::HALT_AT_0}, 0);
    mips::StepResult r = mips::StepResult::Ok;
    for (int i = 0; i < 10 && r == mips::StepResult::Ok; ++i)
        r = cpu.step();
    CHECK(r == mips::StepResult::Exception);
    CHECK(cpu.cp0().last_exception() == mips::ExceptionCode::Sys);
    CHECK(cpu.cp0().epc() == 4u);
    CHECK(cpu.step() == mips::StepResult::Exception);  // fetch from the vector fails
    CHECK(cpu.cp0().last_exception() == mips::ExceptionCode::AdEL);
    CHECK(cpu.cp0().epc() == 4u);
}

// A trapping instruction never retires, on either model: only the ADDI before
// the SYSCALL counts, however many cycles the pipeline needs to drain it.
static void test_trap_does_not_retire() {
    const std::vector<uint32_t> prog = {enc::I(enc::ADDI, enc::zero, enc::t0, 1), enc::SYSCALL,
                                        enc::HALT_AT_0};
    mips::SingleCycleCpu        sc(1u << 16);
    mips::PipelinedCpu          pl(1u << 16);
    for (mips::IProcessor* cpu :
         {static_cast<mips::IProcessor*>(&sc), static_cast<mips::IProcessor*>(&pl)}) {
        CHECK(cpu->load_program(prog, 0));
        std::size_t retired = 0;
        for (int i = 0; i < 10; ++i) {
            (void)cpu->step();
            if (cpu->pipeline_state().retired) ++retired;
        }
        CHECK(retired == 1);
        CHECK(cpu->regs().read(enc::t0) == 1);
    }
}

// ─── main ─────────────────────────────────────────────────────────────────────

// ─── COP0 must not manufacture load-use stalls (#126) ────────────────────────
// MFC0 writes rt and its rs field is a sub-op selector, so neither is a source
// register. MTC0 genuinely reads rt. The loads read a zeroed word past the
// program: loading code bits into Status would trap the CPU into a loop.

// Self-targeting J at word index 2: the halt idiom for a two-instruction program.
static constexpr uint32_t kHaltAt2 = (0x02u << 26) | 0x2u;

static int count_load_stalls(const std::vector<uint32_t>& prog) {
    mips::PipelinedCpu cpu;
    cpu.reset(true);
    CHECK(cpu.load_program(prog));
    int stalls = 0;
    for (int i = 0; i < 100; ++i) {
        const auto r = cpu.step();
        stalls += cpu.pipeline_state().load_stall ? 1 : 0;
        if (r == mips::StepResult::Halt) break;
    }
    return stalls;
}

static void test_mfc0_dest_is_not_a_source() {
    using namespace enc;
    // lw t0 ; mfc0 t0, Status: MFC0 overwrites t0, so there is no dependency.
    CHECK(count_load_stalls({I(LW, zero, t0, 0x100), MFC0(t0, mips::Cp0::kRegStatus), kHaltAt2}) ==
          0);
}

static void test_cop0_selector_is_not_a_source() {
    using namespace enc;
    constexpr uint32_t a0 = 4;  // equals MTC0's rs selector (0x04)
    // lw a0 ; mtc0 t1, Status: the selector 4 must not be read as register $a0.
    CHECK(count_load_stalls({I(LW, zero, a0, 0x100), MTC0(t1, mips::Cp0::kRegStatus), kHaltAt2}) ==
          0);
}

static void test_mtc0_true_dependency_still_stalls() {
    using namespace enc;
    // lw t0 ; mtc0 t0, Status: MTC0 reads t0, so the load-use bubble is real.
    CHECK(count_load_stalls({I(LW, zero, t0, 0x100), MTC0(t0, mips::Cp0::kRegStatus), kHaltAt2}) ==
          1);
}

int main() {
    // Cp0 unit tests
    test_cp0_raise_eret();
    test_cp0_badvaddr();
    test_cp0_read_write();
    test_cp0_reset();

    // SingleCycleCpu exception tests
    test_single_syscall();
    test_single_break();
    test_single_overflow_add();
    test_single_addi_overflow();
    test_single_addr_error_load();
    test_single_mfc0_mtc0();
    test_single_eret();

    // PipelinedCpu exception tests
    test_pipelined_syscall();
    test_pipelined_break();

    // Nested exceptions and retirement accounting
    test_cp0_nested_raise_keeps_epc();
    test_single_epc_survives_unmapped_vector();
    test_pipelined_epc_survives_unmapped_vector();
    test_trap_does_not_retire();

    // COP0 hazard-source decode
    test_mfc0_dest_is_not_a_source();
    test_cop0_selector_is_not_a_source();
    test_mtc0_true_dependency_still_stalls();

    std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
