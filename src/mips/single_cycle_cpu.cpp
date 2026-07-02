#include "mips/single_cycle_cpu.h"
#include "mips/alu.h"
#include "mips/decoder.h"

namespace mips {

// ─── derive_control ───────────────────────────────────────────────────────────
// Defined here so pipelined_cpu.cpp can link against it via mips_core without
// duplicating the switch table. Declared in processor.h.
Control derive_control(const DecodedInstr& instr) {
    Control c;
    switch (instr.format) {
    case InstrFormat::R:
        switch (instr.r().funct) {
        case FunctCode::JR:
            return c;  // no writeback, no mem
        case FunctCode::JALR:
            c.reg_write = true;
            return c;
        default:
            c.reg_write = true;
            c.reg_dst   = true;
            return c;
        }
    case InstrFormat::I:
        switch (instr.opcode) {
        case Opcode::ADDI:
        case Opcode::ADDIU:
        case Opcode::SLTI:
        case Opcode::SLTIU:
            c.reg_write = true;
            c.alu_src   = true;
            c.ext       = Control::Ext::Sign;
            return c;
        case Opcode::ANDI:
        case Opcode::ORI:
        case Opcode::XORI:
        case Opcode::LUI:
            c.reg_write = true;
            c.alu_src   = true;
            c.ext       = Control::Ext::Zero;
            return c;
        case Opcode::LW:
        case Opcode::LBU:
        case Opcode::LHU:
            c.reg_write  = true;
            c.mem_read   = true;
            c.mem_to_reg = true;
            c.alu_src    = true;
            c.ext        = Control::Ext::Sign;
            return c;
        case Opcode::SW:
            c.mem_write = true;
            c.alu_src   = true;
            c.ext       = Control::Ext::Sign;
            return c;
        case Opcode::BEQ:
        case Opcode::BNE:
            c.branch = true;
            c.ext    = Control::Ext::Sign;
            return c;
        default:
            return c;
        }
    case InstrFormat::J:
        c.jump = true;
        if (instr.opcode == Opcode::JAL) c.reg_write = true;
        return c;
    default:
        return c;
    }
}

// ─── SingleCycleCpu ───────────────────────────────────────────────────────────

SingleCycleCpu::SingleCycleCpu(std::size_t mem_bytes) : mem_(mem_bytes) {}

bool SingleCycleCpu::exec_rtype(const DecodedInstr& d, uint32_t pc4, uint32_t& next_pc) {
    const RFields&  r = d.r();
    const FunctCode f = r.funct;

    if (f == FunctCode::JR) {
        next_pc = regs_.read(r.rs);
        return true;
    }
    if (f == FunctCode::JALR) {
        const uint32_t target = regs_.read(r.rs);
        regs_.write(r.rd, pc4);
        next_pc = target;
        return true;
    }

    const auto op = Alu::control(d);
    if (!op) return false;

    const uint32_t a     = regs_.read(r.rs);
    const uint32_t b     = regs_.read(r.rt);
    uint8_t        shamt = r.shamt;
    if (f == FunctCode::SLLV || f == FunctCode::SRLV) shamt = static_cast<uint8_t>(a & 0x1Fu);

    const AluResult res = Alu::execute(*op, a, b, shamt);
    regs_.write(r.rd, res.value);
    return true;
}

bool SingleCycleCpu::exec_itype(const DecodedInstr& d, uint32_t pc4, uint32_t& next_pc) {
    const IFields& i  = d.i();
    const Opcode   op = d.opcode;

    const uint32_t rs_val = regs_.read(i.rs);
    const uint32_t rt_val = regs_.read(i.rt);

    if (op == Opcode::BEQ || op == Opcode::BNE) {
        const auto aluop = Alu::control(d);
        if (!aluop) return false;
        const AluResult res   = Alu::execute(*aluop, rs_val, rt_val);
        const bool      taken = (op == Opcode::BEQ) ? res.zero : !res.zero;
        if (taken) {
            const int32_t off = Decoder::sign_extend(i.imm);
            next_pc           = pc4 + (static_cast<uint32_t>(off) << 2);
        }
        return true;
    }

    const uint32_t imm = (ctrl_.ext == Control::Ext::Sign)
                             ? static_cast<uint32_t>(Decoder::sign_extend(i.imm))
                             : static_cast<uint32_t>(i.imm);

    const auto aluop = Alu::control(d);
    if (!aluop) return false;
    const AluResult res = Alu::execute(*aluop, rs_val, imm);

    if (op == Opcode::LW || op == Opcode::LBU || op == Opcode::LHU) {
        std::optional<uint32_t> loaded;
        switch (op) {
        case Opcode::LW:
            loaded = mem_.read_word(res.value);
            break;
        case Opcode::LBU:
            if (auto v = mem_.read_byte(res.value)) loaded = *v;
            break;
        case Opcode::LHU:
            if (auto v = mem_.read_half(res.value)) loaded = *v;
            break;
        default:
            break;
        }
        if (!loaded) return false;
        regs_.write(i.rt, *loaded);
        return true;
    }

    if (op == Opcode::SW) return mem_.write_word(res.value, rt_val);

    regs_.write(i.rt, res.value);
    return true;
}

StepResult SingleCycleCpu::step() {
    ++cycle_;
    const uint32_t cur_pc  = pc_;
    const auto     fetched = mem_.read_word(cur_pc);
    if (!fetched) return StepResult::Fault;

    const uint32_t pc4     = cur_pc + 4;
    const auto     decoded = Decoder::decode(*fetched);
    if (!decoded) return StepResult::Fault;
    const DecodedInstr& d = *decoded;

    ctrl_            = derive_control(d);
    uint32_t next_pc = pc4;
    bool     ok      = true;

    // Populate EX snapshot (the only "stage" in single-cycle)
    ps_           = {};
    ps_.stages[2] = {"EX", true, false, false, cur_pc, *fetched};
    ps_.cycle     = cycle_;

    switch (d.format) {
    case InstrFormat::J:
        if (d.opcode == Opcode::JAL) regs_.write(kRa, pc4);
        next_pc = (pc4 & 0xF000'0000u) | (d.j().target << 2);
        break;
    case InstrFormat::R:
        ok = exec_rtype(d, pc4, next_pc);
        break;
    case InstrFormat::I:
        ok = exec_itype(d, pc4, next_pc);
        break;
    default:
        return StepResult::Fault;
    }

    if (!ok) return StepResult::Fault;
    pc_ = next_pc;
    return (next_pc == cur_pc) ? StepResult::Halt : StepResult::Ok;
}

bool SingleCycleCpu::load_program(const std::vector<uint32_t>& words, uint32_t addr) {
    if (!mem_.load_words(addr, words)) return false;
    pc_ = addr;
    return true;
}

void SingleCycleCpu::reset(bool clear_memory) {
    regs_.reset();
    pc_    = 0;
    ctrl_  = Control{};
    cycle_ = 0;
    ps_    = {};
    if (clear_memory) mem_.reset();
}

}  // namespace mips
