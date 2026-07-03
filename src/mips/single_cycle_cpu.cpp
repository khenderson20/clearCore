#include "mips/single_cycle_cpu.h"

#include <vector>

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
        if (instr.opcode == Opcode::COP0) return c;  // CP0 ops handle writeback themselves
        switch (instr.r().funct) {
        case FunctCode::JR:
            return c;  // no writeback, no mem
        case FunctCode::JALR:
            c.reg_write = true;
            return c;
        case FunctCode::SYSCALL:
        case FunctCode::BREAK:
            return c;  // no writeback; exception path handles flow
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

StepResult SingleCycleCpu::raise(ExceptionCode code, uint32_t faulting_pc,
                                 uint32_t bad_addr) noexcept {
    pc_ = cp0_.raise(code, faulting_pc, bad_addr);
    return StepResult::Exception;
}

// exec_rtype: returns false only for a genuinely unrecoverable internal error.
// MIPS exceptions (overflow, SYSCALL, BREAK) set exc and return true so the
// caller can forward the Exception result without treating it as a Fault.
bool SingleCycleCpu::exec_rtype(const DecodedInstr& d, uint32_t pc4, uint32_t& next_pc,
                                StepResult& exc) {
    const RFields&  r = d.r();
    const FunctCode f = r.funct;

    // ── COP0 instructions ────────────────────────────────────────────────────
    if (d.opcode == Opcode::COP0) {
        const uint8_t sub = r.rs;
        if (sub == 0x00) {
            // MFC0 rt, rd[sel] — move from CP0 to GPR
            regs_.write(r.rt, cp0_.read(r.rd));
        } else if (sub == 0x04) {
            // MTC0 rt, rd[sel] — move to CP0 from GPR
            cp0_.write(r.rd, regs_.read(r.rt));
        } else if (sub == 0x10 && f == FunctCode::ERET) {
            // ERET — return from exception
            next_pc = cp0_.eret();
        }
        // Unknown COP0 sub-ops are silently ignored (no RI — they may be
        // hazard-control instructions we don't model).
        return true;
    }

    // ── SYSCALL / BREAK ──────────────────────────────────────────────────────
    if (f == FunctCode::SYSCALL) {
        exc = raise(ExceptionCode::Sys, pc4 - 4);
        return true;
    }
    if (f == FunctCode::BREAK) {
        exc = raise(ExceptionCode::Bp, pc4 - 4);
        return true;
    }

    // ── JR / JALR ────────────────────────────────────────────────────────────
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

    // ── ALU R-type ───────────────────────────────────────────────────────────
    const auto op = Alu::control(d);
    if (!op) return false;

    const uint32_t a     = regs_.read(r.rs);
    const uint32_t b     = regs_.read(r.rt);
    uint8_t        shamt = r.shamt;
    if (f == FunctCode::SLLV || f == FunctCode::SRLV) shamt = static_cast<uint8_t>(a & 0x1Fu);

    const AluResult res = Alu::execute(*op, a, b, shamt);

    // Signed overflow on ADD / SUB raises an exception (ADDU / SUBU do not).
    if (res.overflow && (f == FunctCode::ADD || f == FunctCode::SUB)) {
        exc = raise(ExceptionCode::Ov, pc4 - 4);
        return true;
    }

    regs_.write(r.rd, res.value);
    return true;
}

bool SingleCycleCpu::exec_itype(const DecodedInstr& d, uint32_t pc4, uint32_t& next_pc,
                                StepResult& exc) {
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

    // ADDI raises on signed overflow (ADDIU does not).
    if (res.overflow && op == Opcode::ADDI) {
        exc = raise(ExceptionCode::Ov, pc4 - 4);
        return true;
    }

    if (op == Opcode::LW || op == Opcode::LBU || op == Opcode::LHU) {
        const uint32_t          addr = res.value;
        std::optional<uint32_t> loaded;
        switch (op) {
        case Opcode::LW:
            loaded = mem_.read_word(addr);
            break;
        case Opcode::LBU:
            if (auto v = mem_.read_byte(addr)) loaded = *v;
            break;
        case Opcode::LHU:
            if (auto v = mem_.read_half(addr)) loaded = *v;
            break;
        default:
            break;
        }
        if (!loaded) {
            exc = raise(ExceptionCode::AdEL, pc4 - 4, addr);
            return true;
        }
        regs_.write(i.rt, *loaded);
        return true;
    }

    if (op == Opcode::SW) {
        const uint32_t addr = res.value;
        if (!mem_.write_word(addr, rt_val)) {
            exc = raise(ExceptionCode::AdES, pc4 - 4, addr);
            return true;
        }
        return true;
    }

    regs_.write(i.rt, res.value);
    return true;
}

StepResult SingleCycleCpu::step() {
    ++cycle_;
    const uint32_t cur_pc  = pc_;
    const auto     fetched = mem_.read_word(cur_pc);
    if (!fetched) return raise(ExceptionCode::AdEL, cur_pc, cur_pc);

    const uint32_t pc4     = cur_pc + 4;
    const auto     decoded = Decoder::decode(*fetched);
    if (!decoded) return raise(ExceptionCode::RI, cur_pc);
    const DecodedInstr& d = *decoded;

    ctrl_            = derive_control(d);
    uint32_t next_pc = pc4;

    // Populate EX snapshot (the only "stage" in single-cycle)
    ps_           = {};
    ps_.stages[2] = {"EX", true, false, false, cur_pc, *fetched};
    ps_.cycle     = cycle_;

    StepResult exc = StepResult::Ok;  // filled by exec_* if an exception fires

    switch (d.format) {
    case InstrFormat::J:
        if (d.opcode == Opcode::JAL) regs_.write(kRa, pc4);
        next_pc = (pc4 & 0xF000'0000u) | (d.j().target << 2);
        break;
    case InstrFormat::R:
        if (!exec_rtype(d, pc4, next_pc, exc)) return StepResult::Fault;
        break;
    case InstrFormat::I:
        if (!exec_itype(d, pc4, next_pc, exc)) return StepResult::Fault;
        break;
    default:
        return raise(ExceptionCode::RI, cur_pc);
    }

    if (exc == StepResult::Exception) return exc;

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
    cp0_.reset();
    pc_    = 0;
    hi_    = 0;
    lo_    = 0;
    ctrl_  = Control{};
    cycle_ = 0;
    ps_    = {};
    if (clear_memory) mem_.reset();
}

}  // namespace mips
