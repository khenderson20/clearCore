// Hazard-detection unit, forwarding unit and the shared load/store helper,
// tested directly on pipeline-register values without stepping a CPU (#220).

#include "mips/data_memory.h"
#include "mips/pipeline_units.h"
#include "mips/processor.h"

#include <cstdint>
#include <cstdio>

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
constexpr uint32_t cop0(uint32_t sub, uint32_t rt, uint32_t rd, uint32_t funct = 0) {
    return (0x10u << 26) | (sub << 21) | (rt << 16) | (rd << 11) | funct;
}
constexpr uint32_t zero = 0, t0 = 8, t1 = 9, t2 = 10;
constexpr uint32_t ADD = 0x20, SLL = 0x00, JR = 0x08, JALR = 0x09;
constexpr uint32_t ADDI = 0x08, LUI = 0x0F, LW = 0x23, SW = 0x2B, BEQ = 0x04;
constexpr uint32_t J_TO_0 = 0x0800'0000u;
}  // namespace enc

static mips::SourceRegisters sources(uint32_t word) {
    const auto d = mips::Decoder::decode(word);
    return d ? mips::source_registers(*d) : mips::SourceRegisters{-2, -2};
}

static bool reads(uint32_t word, int a, int b) {
    const auto s = sources(word);
    return s.a == a && s.b == b;
}

// The ID/EX register as the decode stage would fill it for `word`.
static mips::IdEx in_ex(uint32_t word) {
    const auto d = mips::Decoder::decode(word);
    mips::IdEx r{};
    r.valid   = d.has_value();
    r.decoded = d.value_or(mips::DecodedInstr{});
    r.ctrl    = d ? mips::derive_control(*d) : mips::Control{};
    if (d && d->format == mips::InstrFormat::R) {
        r.rs = d->r().rs;
        r.rt = d->r().rt;
    } else if (d && d->format == mips::InstrFormat::I) {
        r.rs = d->i().rs;
        r.rt = d->i().rt;
    }
    return r;
}

static mips::IfId in_id(uint32_t word) {
    mips::IfId r{};
    r.valid = true;
    r.instr = word;
    return r;
}

static void test_source_registers() {
    using namespace enc;
    CHECK(reads(R(t0, t1, t2, 0, ADD), t0, t1));     // arithmetic: rs, rt
    CHECK(reads(R(zero, t1, t2, 4, SLL), -1, t1));   // shift: rt only
    CHECK(reads(R(t0, zero, zero, 0, JR), t0, -1));  // jr: rs only
    CHECK(reads(R(t0, zero, 31, 0, JALR), t0, -1));  // jalr: rs only
    CHECK(reads(I(ADDI, t0, t1, 5), t0, -1));        // ALU-immediate: rs only
    CHECK(reads(I(LW, t0, t1, 0), t0, -1));          // load: base only
    CHECK(reads(I(LUI, zero, t1, 1), -1, -1));       // lui: nothing
    CHECK(reads(I(SW, t0, t1, 0), t0, t1));          // store: base and data
    CHECK(reads(I(BEQ, t0, t1, 0), t0, t1));         // branch: both operands
    CHECK(reads(J_TO_0, -1, -1));                    // jump: target bits, no registers
    CHECK(reads(cop0(0x04, t1, 12), -1, t1));        // mtc0: rt only
    CHECK(reads(cop0(0x00, t1, 12), -1, -1));        // mfc0 writes rt, reads nothing
    CHECK(reads(cop0(0x10, 0, 0, 0x18), -1, -1));    // eret
}

static int dest(uint32_t word) {
    const auto d = mips::Decoder::decode(word);
    return d ? mips::destination_register(*d) : -2;
}

static void test_destination_register() {
    using namespace enc;
    constexpr uint32_t SYSCALL = 0x0C, BNE = 0x05, JAL_TO_0 = 0x0C00'0000u;
    CHECK(dest(R(t0, t1, t2, 0, ADD)) == static_cast<int>(t2));    // R-format: rd
    CHECK(dest(R(zero, t1, t2, 4, SLL)) == static_cast<int>(t2));  // shift: rd
    CHECK(dest(0) == 0);                                           // nop selects $zero
    CHECK(dest(I(ADDI, t0, t1, 5)) == static_cast<int>(t1));       // ALU-immediate: rt
    CHECK(dest(I(LUI, zero, t1, 1)) == static_cast<int>(t1));      // lui: rt
    CHECK(dest(I(LW, t0, t1, 0)) == static_cast<int>(t1));         // load: rt
    CHECK(dest(I(SW, t0, t1, 0)) == -1);                           // store: none
    CHECK(dest(I(BEQ, t0, t1, 0)) == -1);                          // branches: none
    CHECK(dest(I(BNE, t0, t1, 0)) == -1);
    CHECK(dest(J_TO_0) == -1);                                      // j: none
    CHECK(dest(JAL_TO_0) == 31);                                    // jal links $ra
    CHECK(dest(R(t0, zero, t2, 0, JALR)) == static_cast<int>(t2));  // jalr links rd
    CHECK(dest(R(t0, zero, zero, 0, JR)) == -1);                    // jr: none
    CHECK(dest(R(zero, zero, zero, 0, SYSCALL)) == -1);             // syscall: none
    // COP0 decodes as R-format; rd is a CP0 register, never the GPR written.
    CHECK(dest(cop0(0x00, t1, 12)) == static_cast<int>(t1));  // mfc0 writes rt
    CHECK(dest(cop0(0x04, t1, 12)) == -1);                    // mtc0 writes CP0
    CHECK(dest(cop0(0x10, 0, 0, 0x18)) == -1);                // eret
}

static void test_load_use_hazard() {
    using namespace enc;
    const mips::IdEx load_t0 = in_ex(I(LW, zero, t0, 0));

    CHECK(mips::load_use_hazard(load_t0, in_id(R(t0, t1, t2, 0, ADD))));    // reads t0 as rs
    CHECK(mips::load_use_hazard(load_t0, in_id(R(t1, t0, t2, 0, ADD))));    // reads t0 as rt
    CHECK(mips::load_use_hazard(load_t0, in_id(R(zero, t0, t2, 3, SLL))));  // shift reads rt
    CHECK(mips::load_use_hazard(load_t0, in_id(I(SW, t1, t0, 0))));         // store data
    CHECK(mips::load_use_hazard(load_t0, in_id(cop0(0x04, t0, 12))));       // mtc0 reads rt

    CHECK(!mips::load_use_hazard(load_t0, in_id(R(t1, t2, t0, 0, ADD))));  // writes t0 only
    CHECK(!mips::load_use_hazard(load_t0, in_id(I(LUI, zero, t0, 1))));    // lui reads nothing
    CHECK(!mips::load_use_hazard(load_t0, in_id(I(ADDI, t1, t0, 1))));     // imm: rt is a dest
    CHECK(!mips::load_use_hazard(load_t0, in_id(cop0(0x00, t0, 12))));     // mfc0 writes rt
    CHECK(!mips::load_use_hazard(load_t0, in_id(J_TO_0)));                 // no phantom stall
    CHECK(!mips::load_use_hazard(load_t0, in_id(0xFFFF'FFFFu)));           // undecodable

    // A load into $zero never creates a dependency.
    CHECK(!mips::load_use_hazard(in_ex(I(LW, t1, zero, 0)), in_id(R(zero, zero, t2, 0, ADD))));
    // Only a load in EX can stall; an ALU result is forwarded instead.
    CHECK(!mips::load_use_hazard(in_ex(I(ADDI, zero, t0, 1)), in_id(R(t0, t1, t2, 0, ADD))));

    // Bubbles never stall.
    mips::IdEx bubble_ex = load_t0;
    bubble_ex.valid      = false;
    CHECK(!mips::load_use_hazard(bubble_ex, in_id(R(t0, t1, t2, 0, ADD))));
    mips::IfId bubble_id = in_id(R(t0, t1, t2, 0, ADD));
    bubble_id.valid      = false;
    CHECK(!mips::load_use_hazard(load_t0, bubble_id));
}

static mips::ExMem ex_mem_writing(uint8_t reg, uint32_t value, bool is_load = false) {
    mips::ExMem r{};
    r.valid          = true;
    r.ctrl.reg_write = true;
    r.ctrl.mem_read  = is_load;
    r.write_reg      = reg;
    r.alu.value      = value;
    return r;
}

static mips::MemWb mem_wb_writing(uint8_t reg, uint32_t alu, uint32_t loaded, bool is_load) {
    mips::MemWb r{};
    r.valid           = true;
    r.ctrl.reg_write  = true;
    r.ctrl.mem_to_reg = is_load;
    r.write_reg       = reg;
    r.alu_val         = alu;
    r.mem_val         = loaded;
    return r;
}

static void test_forward_operands() {
    using namespace enc;
    mips::IdEx add = in_ex(R(t0, t1, t2, 0, ADD));
    add.rs_val     = 1;  // stale register-file values
    add.rt_val     = 2;

    {  // nothing in flight: the register-file values stand
        const auto ops = mips::forward_operands(add, {}, {});
        CHECK(ops.a == 1 && ops.b == 2);
        CHECK(!ops.paths.ex_mem_a && !ops.paths.ex_mem_b);
        CHECK(!ops.paths.mem_wb_a && !ops.paths.mem_wb_b);
    }
    {  // EX/MEM → A
        const auto ops = mips::forward_operands(add, ex_mem_writing(t0, 100), {});
        CHECK(ops.a == 100 && ops.b == 2);
        CHECK(ops.paths.ex_mem_a && !ops.paths.ex_mem_b);
    }
    {  // MEM/WB → B, with the loaded word (not the address) for a load
        const auto ops = mips::forward_operands(add, {}, mem_wb_writing(t1, 0x40, 77, true));
        CHECK(ops.a == 1 && ops.b == 77);
        CHECK(ops.paths.mem_wb_b && !ops.paths.mem_wb_a);
    }
    {  // both paths write the same register: EX/MEM (the younger result) wins
        const auto ops =
            mips::forward_operands(add, ex_mem_writing(t0, 100), mem_wb_writing(t0, 50, 0, false));
        CHECK(ops.a == 100);
        CHECK(ops.paths.ex_mem_a && !ops.paths.mem_wb_a);
    }
    {  // a load in EX/MEM holds the address, so it must not forward
        const auto ops = mips::forward_operands(add, ex_mem_writing(t0, 0x1000, true), {});
        CHECK(ops.a == 1 && !ops.paths.ex_mem_a);
    }
    {  // $zero is never forwarded, even when something "writes" it
        mips::IdEx reads_zero = in_ex(R(zero, zero, t2, 0, ADD));
        const auto ops        = mips::forward_operands(reads_zero, ex_mem_writing(zero, 9),
                                                       mem_wb_writing(zero, 9, 9, false));
        CHECK(ops.a == 0 && ops.b == 0);
        CHECK(!ops.paths.ex_mem_a && !ops.paths.mem_wb_a);
    }
    {  // bubbles forward nothing
        mips::ExMem bubble = ex_mem_writing(t0, 100);
        bubble.valid       = false;
        const auto ops     = mips::forward_operands(add, bubble, {});
        CHECK(ops.a == 1 && !ops.paths.ex_mem_a);
    }
}

static void test_load_store_helpers() {
    isa::Memory mem(16);
    CHECK(mips::store_data(mem, mips::Opcode::SW, 4, 0x8180'FF7Fu));
    CHECK(mips::load_data(mem, mips::Opcode::LW, 4).value_or(0) == 0x8180'FF7Fu);
    CHECK(mips::load_data(mem, mips::Opcode::LBU, 4).value_or(0) == 0x7Fu);    // zero-extended
    CHECK(mips::load_data(mem, mips::Opcode::LBU, 7).value_or(0) == 0x81u);    // not sign-extended
    CHECK(mips::load_data(mem, mips::Opcode::LHU, 6).value_or(0) == 0x8180u);  // zero-extended

    CHECK(!mips::load_data(mem, mips::Opcode::LW, 2).has_value());    // misaligned
    CHECK(!mips::load_data(mem, mips::Opcode::LW, 16).has_value());   // out of range
    CHECK(!mips::load_data(mem, mips::Opcode::ADDI, 0).has_value());  // not a load
    CHECK(!mips::store_data(mem, mips::Opcode::SW, 1, 0));            // misaligned
    CHECK(!mips::store_data(mem, mips::Opcode::SW, 16, 0));           // out of range
    CHECK(!mips::store_data(mem, mips::Opcode::LW, 0, 0));            // not a store
}

int main() {
    test_source_registers();
    test_destination_register();
    test_load_use_hazard();
    test_forward_operands();
    test_load_store_helpers();

    std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
