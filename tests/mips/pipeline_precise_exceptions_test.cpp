// Pipelined exception precision: squashed (wrong-path) and younger instructions
// must never trap or redirect, and an older instruction must never be lost to a
// younger one's trap. SingleCycleCpu has no such hazard and is the oracle: every
// program here must behave identically on both models.
// Regression tests for the ordering bugs described in issue #218, the three
// IF/ID cases found while refactoring for #220, and a randomized differential.

#include "mips/pipelined_cpu.h"
#include "mips/single_cycle_cpu.h"

#include <cstdint>
#include <cstdio>
#include <random>
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
constexpr uint32_t zero = 0, t0 = 8, t1 = 9, t2 = 10;
constexpr uint32_t ADDI = 0x08, BEQ = 0x04, LW = 0x23;
constexpr uint32_t F_JR     = 0x08;
constexpr uint32_t SYSCALL  = 0x0C;
constexpr uint32_t kIllegal = 0xFFFF'FFFFu;  // opcode 0x3F: not decodable

// mtc0 $rt, $rd — COP0 opcode, MT sub-op in the rs field.
constexpr uint32_t mtc0(uint32_t rt, uint32_t rd) {
    return (0x10u << 26) | (0x04u << 21) | (rt << 16) | (rd << 11);
}

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

// A faulting load (MEM) with an MTC0 right behind it (EX): the MTC0 is squashed
// with everything younger than the fault, so it must not write CP0 either.
static void test_squashed_mtc0_writes_no_cp0_register() {
    using namespace enc;
    const std::vector<uint32_t> prog = {I(ADDI, zero, t0, 1),             // t0 = 1 (misaligned)
                                        I(ADDI, zero, t2, 1),             // t2 = Status.IE
                                        I(LW, t0, t1, 0),                 // AdEL in MEM
                                        mtc0(t2, mips::Cp0::kRegStatus),  // in EX, same cycle
                                        halt_at(16)};
    mips::SingleCycleCpu        sc(1u << 12);
    mips::PipelinedCpu          pl(1u << 12);
    for (mips::IProcessor* cpu :
         {static_cast<mips::IProcessor*>(&sc), static_cast<mips::IProcessor*>(&pl)}) {
        CHECK(cpu->load_program(prog, 0));
        CHECK(run_to_stop(*cpu) == mips::StepResult::Exception);
    }
    // Exception entry sets only EXL; a leaked MTC0 would also have set IE (bit 0).
    CHECK(sc.cp0().status() == mips::Cp0::kStatusEXL);  // oracle
    CHECK(pl.cp0().status() == mips::Cp0::kStatusEXL);
    CHECK(pl.cp0().last_exception() == mips::ExceptionCode::AdEL);
    CHECK(pl.cp0().epc() == 8u);
}

// An illegal word right behind a faulting load: the load is older, so its AdEL
// is the exception, even though ID sees the illegal word a cycle before the
// load reaches MEM. (Raising RI in ID used to report the younger RI.)
static void test_older_load_fault_wins_over_younger_illegal_word() {
    using namespace enc;
    const std::vector<uint32_t> prog = {I(ADDI, zero, t0, 1),  // t0 = 1 (misaligned)
                                        I(LW, t0, t1, 0),      // AdEL in MEM
                                        kIllegal, halt_at(12)};
    mips::SingleCycleCpu        sc(1u << 12);
    mips::PipelinedCpu          pl(1u << 12);
    for (mips::IProcessor* cpu :
         {static_cast<mips::IProcessor*>(&sc), static_cast<mips::IProcessor*>(&pl)}) {
        CHECK(cpu->load_program(prog, 0));
        CHECK(run_to_stop(*cpu) == mips::StepResult::Exception);
    }
    CHECK(sc.cp0().last_exception() == mips::ExceptionCode::AdEL);  // oracle
    CHECK(pl.cp0().last_exception() == mips::ExceptionCode::AdEL);
    CHECK(pl.cp0().epc() == 4u);
    CHECK(pl.cp0().bad_vaddr() == 1u);
}

// Running off the end of memory: the fetch fault must not take the older
// instruction still in ID down with it. (It used to be flushed, so $t1 was
// never written.)
static void test_fetch_past_end_keeps_older_instructions() {
    using namespace enc;
    mips::PipelinedCpu pl(8);  // exactly the two instructions below
    CHECK(pl.load_program({I(ADDI, zero, t0, 5), I(ADDI, zero, t1, 7)}, 0));
    CHECK(run_to_stop(pl) == mips::StepResult::Exception);
    CHECK(pl.cp0().last_exception() == mips::ExceptionCode::AdEL);
    CHECK(pl.cp0().epc() == 8u);
    CHECK(pl.cp0().bad_vaddr() == 8u);
    // The second ADDI was in MEM when the fetch fault reached EX; it retires
    // in the next cycle, like any instruction older than a trap.
    (void)pl.step();
    CHECK(pl.regs().read(t0) == 5u);
    CHECK(pl.regs().read(t1) == 7u);
}

// A J resolved in ID redirects away from the next sequential fetch; if that
// fetch is past the end of memory it is on the wrong path and must not trap.
static void test_jump_in_id_wrong_path_fetch_past_end() {
    using namespace enc;
    expect_both_halt(
        [](mips::IProcessor& cpu) {
            // 8 bytes: halt at 0, `j 0` at 4. While the j is in ID, IF fetches 8.
            CHECK(cpu.load_program({halt_at(0), halt_at(0)}, 0));
            cpu.set_pc(4);
        },
        8);
}

// COP0 decodes as R-format, so a COP0 word whose low six bits happen to equal
// the BREAK funct must still be a COP0 operation (an unknown one, ignored),
// not a breakpoint.
static void test_cop0_word_with_break_funct_is_not_break() {
    using namespace enc;
    expect_both_halt(
        [](mips::IProcessor& cpu) {
            const uint32_t cop0_unknown = (0x10u << 26) | (0x0Au << 21) | 0x0Du;
            CHECK(cpu.load_program({cop0_unknown, halt_at(4)}, 0));
        },
        1u << 12);
}

// ─── Randomized differential against the oracle ──────────────────────────────
// Random programs over every instruction class the models implement, including
// faulting loads/stores, illegal words, COP0, SYSCALL/BREAK and jumps. For each
// one the pipelined model must stop the way the oracle does: the same halt with
// the same registers and memory, or the same first exception (code, EPC,
// BadVAddr) with the same registers and memory once the instructions older than
// the trap have drained. Two known, deliberate model differences are skipped:
// self-modifying code (a pipeline has already fetched the old word) and a halt
// on a self-loop that is not `j`/`jal` (only the single-cycle model treats
// those as halts). The generator uses mt19937 output directly, which the
// standard fully specifies, so every platform runs the same programs.
namespace fuzz {

class Gen {
public:
    explicit Gen(uint32_t seed) : rng_(seed) {}
    uint32_t pick(uint32_t lo, uint32_t hi) {
        return lo + static_cast<uint32_t>(rng_() % (hi - lo + 1));
    }
    uint32_t word() { return static_cast<uint32_t>(rng_()); }

private:
    std::mt19937 rng_;
};

std::vector<uint32_t> program(Gen& g, std::size_t n, uint32_t mem_bytes) {
    using namespace enc;
    auto                      reg      = [&] { return g.pick(0, 6) == 0 ? 0u : g.pick(8, 13); };
    static constexpr uint32_t kFunct[] = {0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
                                          0x2A, 0x2B, 0x00, 0x02, 0x03, 0x04, 0x06};
    std::vector<uint32_t>     p;
    for (std::size_t k = 0; k < n; ++k) {
        const uint32_t kind = g.pick(0, 99);
        uint32_t       w    = 0;
        if (kind < 30) {
            w = R(reg(), reg(), reg(), g.pick(0, 31), kFunct[g.pick(0, 14)]);
        } else if (kind < 50) {
            const auto imm = g.pick(0, 3) == 0 ? g.pick(0, 0xFFFF) : g.pick(0, 40);
            w              = I(g.pick(0x08, 0x0F), reg(), reg(), static_cast<uint16_t>(imm));
        } else if (kind < 62) {
            static constexpr uint32_t kMem[] = {0x23, 0x24, 0x25, 0x2B};  // lw lbu lhu sw
            const auto off = g.pick(0, 4) == 0 ? g.pick(0, 0xFFFF) : g.pick(0, mem_bytes / 4) * 4;
            w              = I(kMem[g.pick(0, 3)], g.pick(0, 3) == 0 ? 0 : reg(), reg(),
                               static_cast<uint16_t>(off));
        } else if (kind < 72) {
            const auto off = static_cast<uint16_t>(static_cast<int32_t>(g.pick(0, 8)) - 4);
            w              = I(g.pick(0, 1) ? 0x04u : 0x05u, reg(), reg(), off);  // beq / bne
        } else if (kind < 77) {
            w = (g.pick(0x02, 0x03) << 26) | g.pick(0, static_cast<uint32_t>(n));  // j / jal
        } else if (kind < 80) {
            w = R(reg(), 0, g.pick(0, 1) ? 31u : reg(), 0, g.pick(0, 1) ? 0x08u : 0x09u);
        } else if (kind < 82) {
            w = R(0, 0, 0, 0, g.pick(0, 1) ? 0x0Cu : 0x0Du);  // syscall / break
        } else if (kind < 88) {
            w = (0x10u << 26) | (g.pick(0, 1) ? 0x04u << 21 : 0u) | (reg() << 16) |
                (g.pick(8, 14) << 11);  // mtc0 / mfc0
        } else if (kind < 89) {
            w = (0x10u << 26) | (0x10u << 21) | 0x18u;  // eret
        } else if (kind < 94) {
            w = g.pick(0, 1) ? g.word() : kIllegal;
        } else if (kind < 96) {
            w = halt_at(static_cast<uint32_t>(k * 4));
        }
        p.push_back(w);  // kind >= 96: nop
    }
    return p;
}

bool same_registers(const mips::IProcessor& a, const mips::IProcessor& b) {
    for (uint8_t r = 1; r < 32; ++r)
        if (a.regs().read(r) != b.regs().read(r)) return false;
    return true;
}

bool same_memory(const mips::IProcessor& a, const mips::IProcessor& b) {
    const auto x = a.mem().raw();
    const auto y = b.mem().raw();
    for (std::size_t i = 0; i < x.size(); ++i)
        if (x[i] != y[i]) return false;
    return true;
}

}  // namespace fuzz

static void test_random_programs_match_oracle() {
    fuzz::Gen g(20260927);
    int       compared = 0, mismatched = 0;
    for (int t = 0; t < 4000; ++t) {
        const uint32_t mem_bytes = (t % 3 == 0) ? 64 : (t % 3 == 1) ? 256 : 4096;
        const auto     prog      = fuzz::program(g, 4 + g.pick(0, 27), mem_bytes);
        if (prog.size() * 4 > mem_bytes) continue;

        mips::SingleCycleCpu sc(mem_bytes);
        mips::PipelinedCpu   pl(mem_bytes);
        CHECK(sc.load_program(prog, 0) && pl.load_program(prog, 0));

        mips::StepResult a      = mips::StepResult::Ok;
        uint32_t         max_pc = 0;
        for (int i = 0; i < 400 && a == mips::StepResult::Ok; ++i) {
            if (sc.pc() < mem_bytes && sc.pc() > max_pc) max_pc = sc.pc();
            a = sc.step();
        }
        if (a == mips::StepResult::Ok) continue;  // still looping: nothing to compare

        bool self_modifying = false;
        for (uint32_t addr = 0; addr + 4 <= mem_bytes && addr <= max_pc && !self_modifying;
             addr += 4) {
            const uint32_t original = addr / 4 < prog.size() ? prog[addr / 4] : 0;
            self_modifying          = sc.mem().read_word(addr).value_or(0) != original;
        }
        if (self_modifying) continue;
        if (a == mips::StepResult::Halt) {
            const uint32_t op = sc.mem().read_word(sc.pc()).value_or(0) >> 26;
            if (op != 0x02 && op != 0x03) continue;  // halted on a non-j self-loop
        }

        mips::StepResult b = mips::StepResult::Ok;
        for (int i = 0; i < 2400 && b == mips::StepResult::Ok; ++i)
            b = pl.step();

        bool ok = a == b;
        if (ok && a == mips::StepResult::Exception) {
            ok = sc.cp0().last_exception() == pl.cp0().last_exception() &&
                 sc.cp0().epc() == pl.cp0().epc() && sc.cp0().bad_vaddr() == pl.cp0().bad_vaddr();
            for (int d = 0; d < 3; ++d)
                (void)pl.step();  // let the instructions older than the trap retire
        }
        ok = ok && fuzz::same_registers(sc, pl) && fuzz::same_memory(sc, pl);
        ++compared;
        if (!ok) {
            ++mismatched;
            if (mismatched <= 3) {
                std::fprintf(stderr, "mismatch, program %d:", t);
                for (const uint32_t w : prog)
                    std::fprintf(stderr, " %08x", w);
                std::fprintf(stderr, "\n");
            }
        }
    }
    CHECK(compared > 1000);  // the filters must not hollow the test out
    CHECK(mismatched == 0);
    std::printf("random differential: %d programs compared, %d mismatched\n", compared, mismatched);
}

int main() {
    test_taken_branch_over_illegal_word();
    test_jr_over_illegal_words();
    test_taken_branch_wrong_path_fetch_past_end();
    test_older_fault_wins_over_younger_syscall();
    test_squashed_mtc0_writes_no_cp0_register();
    test_older_load_fault_wins_over_younger_illegal_word();
    test_fetch_past_end_keeps_older_instructions();
    test_jump_in_id_wrong_path_fetch_past_end();
    test_cop0_word_with_break_funct_is_not_break();
    test_random_programs_match_oracle();

    std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
