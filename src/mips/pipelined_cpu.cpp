#include "mips/pipelined_cpu.h"

#include <string>
#include <vector>

#include "mips/alu.h"
#include "mips/data_memory.h"
#include "mips/decoder.h"
#include "mips/disassembler.h"
#include "mips/trace.h"

// ─── pipelined_cpu.cpp ───────────────────────────────────────────────────────
// Each step() models one clock cycle.  The five stages run in the order
// WB → MEM → EX → ID → IF so that register-file writes (WB) are visible to
// register-file reads (ID) within the same cycle — the "first-half write /
// second-half read" convention from H&H §8.4.
//
// Every stage reads only the pipeline registers as they were at the start of
// the cycle.  WB and MEM commit their own effects (the register write, the
// load or store), because no stage is older than they are.  EX, ID and IF
// change nothing: they return an outcome, and step() applies it only if no
// older stage trapped or redirected in the same cycle.  That age order is what
// keeps a squashed (wrong-path or younger) instruction from trapping,
// redirecting or writing CP0.

namespace mips {

PipelinedCpu::PipelinedCpu(std::size_t mem_bytes) : mem_(mem_bytes) {}

// ─── WB ──────────────────────────────────────────────────────────────────────
bool PipelinedCpu::write_back(const MemWb& in) {
    if (!in.valid) return false;
    ctrl_ = in.ctrl;
    if (in.ctrl.reg_write && in.write_reg != 0)
        regs_.write(in.write_reg, in.ctrl.mem_to_reg ? in.mem_val : in.alu_val);
    return in.is_halt;
}

// ─── MEM ─────────────────────────────────────────────────────────────────────
PipelinedCpu::MemOutcome PipelinedCpu::memory_access(const ExMem& in) {
    MemOutcome out;
    if (!in.valid) return out;

    MemWb& next    = out.next;
    next.valid     = true;
    next.pc        = in.pc;
    next.raw       = in.raw;
    next.ctrl      = in.ctrl;
    next.alu_val   = in.alu.value;
    next.write_reg = in.write_reg;
    next.is_halt   = in.is_halt;

    const uint32_t addr = in.alu.value;
    if (in.ctrl.mem_read) {
        const auto loaded = load_data(mem_, in.opcode, addr);
        if (!loaded) {
            out.trap = Trap{ExceptionCode::AdEL, in.pc, addr};
            out.next = {};
            return out;
        }
        next.mem_val = *loaded;
    }
    if (in.ctrl.mem_write && !store_data(mem_, in.opcode, addr, in.rt_val)) {
        out.trap = Trap{ExceptionCode::AdES, in.pc, addr};
        out.next = {};
    }
    return out;
}

// ─── EX ──────────────────────────────────────────────────────────────────────
PipelinedCpu::ExOutcome PipelinedCpu::execute(const IdEx& in, const ExMem& ex_mem,
                                              const MemWb& mem_wb) const {
    ExOutcome out;
    if (!in.valid) return out;

    // Note: mem_wb is the MEM/WB register BEFORE WB wrote the register file,
    // so its value is still needed for the MEM/WB → EX path.
    const ExOperands ops = forward_operands(in, ex_mem, mem_wb);
    out.forwarded        = ops.paths;

    const DecodedInstr& dec = in.decoded;

    // Every instruction that does not trap continues into EX/MEM — including
    // branches, jumps and CP0 ops that write no register. They travel through
    // MEM and WB as no-ops, exactly as the hardware does, so the trace grid
    // shows all five stages for them and they count as retired. Fill the
    // common fields once; each case below only flips `valid` and sets what it
    // produces. A trapping instruction leaves `valid` false and becomes a
    // bubble at the end.
    ExMem& next  = out.next;
    next.pc      = in.pc;
    next.raw     = dec.raw;
    next.ctrl    = in.ctrl;
    next.opcode  = dec.opcode;
    next.rt_val  = ops.b;  // forwarded rt: the data SW stores
    next.is_halt = in.is_halt;

    if (dec.format == InstrFormat::J) {
        // ── J / JAL: the jump was resolved in ID; EX only produces JAL's link.
        next.valid = true;
        if (dec.opcode == Opcode::JAL) {
            next.alu.value = in.pc4;  // return address
            next.write_reg = 31;      // $ra
        }
    } else if (dec.format == InstrFormat::I &&
               (dec.opcode == Opcode::BEQ || dec.opcode == Opcode::BNE)) {
        // ── BEQ / BNE: resolved in EX, 2-stage flush when taken.
        const AluResult cmp   = Alu::execute(AluOp::SUBU, ops.a, ops.b);
        const bool      taken = (dec.opcode == Opcode::BEQ) ? cmp.zero : !cmp.zero;
        if (taken) {
            const int32_t off = Decoder::sign_extend(dec.i().imm);
            out.redirect      = in.pc4 + (static_cast<uint32_t>(off) << 2);
        }
        next.valid = true;  // no writeback; flows through MEM/WB as a no-op
    } else if (dec.format == InstrFormat::R &&
               (dec.r().funct == FunctCode::JR || dec.r().funct == FunctCode::JALR)) {
        // ── JR / JALR: register jump resolved in EX, 2-stage flush.
        out.redirect = ops.a;  // rs, possibly forwarded
        next.valid   = true;
        if (dec.r().funct == FunctCode::JALR) {
            next.alu.value = in.pc4;  // return address
            next.write_reg = dec.r().rd;
        }
    } else if (dec.format == InstrFormat::R &&
               (dec.r().funct == FunctCode::SYSCALL || dec.r().funct == FunctCode::BREAK)) {
        // ── SYSCALL / BREAK: trap; the handler gets a clean pipeline.
        const ExceptionCode code =
            (dec.r().funct == FunctCode::SYSCALL) ? ExceptionCode::Sys : ExceptionCode::Bp;
        out.trap = Trap{code, in.pc, 0};
    } else if (dec.opcode == Opcode::COP0) {
        // ── COP0: MFC0 / MTC0 / ERET.
        const uint8_t sub = dec.r().rs;
        next.valid        = true;
        if (sub == 0x00) {
            // MFC0: the CP0 value reaches the GPR in WB via the ALU-result path.
            next.ctrl.reg_write = true;
            next.alu.value      = cp0_.read(dec.r().rd);
            next.write_reg      = dec.r().rt;
        } else if (sub == 0x04) {
            out.mtc0 = Mtc0Write{dec.r().rd, ops.b};  // MTC0: forwarded GPR → CP0
        } else if (sub == 0x10 && dec.r().funct == FunctCode::ERET) {
            // ERET: clear EXL and jump to EPC, treated as a register jump.
            out.eret     = true;
            out.redirect = cp0_.epc();
        }
        // Unknown COP0 sub-ops are silently ignored (EHB, etc.).
    } else {
        // ── Regular ALU instruction.
        const auto aluop = Alu::control(dec);
        if (!aluop) {
            out.trap = Trap{ExceptionCode::RI, in.pc, 0};  // decodes, but EX cannot run it
        } else {
            uint8_t shamt = (dec.format == InstrFormat::R) ? dec.r().shamt : 0;
            if (dec.format == InstrFormat::R) {
                const auto f = dec.r().funct;
                if (f == FunctCode::SLLV || f == FunctCode::SRLV)
                    shamt = static_cast<uint8_t>(ops.a & 0x1Fu);
            }

            // ALU B: the register value or the sign/zero-extended immediate.
            uint32_t alu_b = ops.b;
            if (in.ctrl.alu_src && dec.format == InstrFormat::I) {
                alu_b = (in.ctrl.ext == Control::Ext::Sign)
                            ? static_cast<uint32_t>(Decoder::sign_extend(dec.i().imm))
                            : static_cast<uint32_t>(dec.i().imm);
            }

            const AluResult alu_res = Alu::execute(*aluop, ops.a, alu_b, shamt);

            // Signed overflow on ADD/SUB/ADDI raises Ov (ADDU/SUBU/ADDIU do not).
            const auto funct = (dec.format == InstrFormat::R) ? dec.r().funct : FunctCode::SLL;
            const bool is_signed_op = (funct == FunctCode::ADD || funct == FunctCode::SUB) ||
                                      (dec.opcode == Opcode::ADDI);
            if (alu_res.overflow && is_signed_op) {
                out.trap = Trap{ExceptionCode::Ov, in.pc, 0};
            } else {
                // Destination register: rd (R-type) or rt (I-type).
                uint8_t write_reg = 0;
                if (in.ctrl.reg_dst && dec.format == InstrFormat::R)
                    write_reg = dec.r().rd;
                else if (dec.format == InstrFormat::I)
                    write_reg = dec.i().rt;

                next.valid     = true;
                next.alu       = alu_res;
                next.write_reg = write_reg;
            }
        }
    }

    // A trapping instruction is squashed: leave a clean bubble rather than a
    // half-filled register carrying stale pc/raw fields.
    if (!next.valid) next = {};
    return out;
}

// ─── ID ──────────────────────────────────────────────────────────────────────
PipelinedCpu::IdOutcome PipelinedCpu::decode(const IfId& in) const {
    IdOutcome  out;
    const auto decoded = Decoder::decode(in.instr);
    if (!decoded) {
        out.trap = Trap{ExceptionCode::RI, in.pc, 0};
        return out;
    }
    const DecodedInstr& dec = *decoded;

    // Register indices, needed by the hazard unit next cycle and by the
    // forwarding unit in EX.
    uint8_t rs = 0;
    uint8_t rt = 0;
    if (dec.format == InstrFormat::R) {
        rs = dec.r().rs;
        rt = dec.r().rt;
    } else if (dec.format == InstrFormat::I) {
        rs = dec.i().rs;
        rt = dec.i().rt;
    }

    // The register file is read AFTER WB wrote it (WB ran first this cycle).
    IdEx& next   = out.next;
    next.valid   = true;
    next.pc      = in.pc;
    next.pc4     = in.pc4;
    next.ctrl    = derive_control(dec);
    next.decoded = dec;
    next.rs_val  = regs_.read(rs);
    next.rt_val  = regs_.read(rt);
    next.rs      = rs;
    next.rt      = rt;
    next.is_halt = in.is_halt;

    // J / JAL: the target is computable from the instruction bits alone, so it
    // is resolved here for a 1-cycle penalty (H&H Figure 8.30).
    if (dec.format == InstrFormat::J) out.jump = (in.pc4 & 0xF000'0000u) | (dec.j().target << 2);
    return out;
}

// ─── IF ──────────────────────────────────────────────────────────────────────
PipelinedCpu::IfOutcome PipelinedCpu::fetch() const {
    IfOutcome  out;
    const auto word = mem_.read_word(pc_);
    if (!word) {
        out.trap = Trap{ExceptionCode::AdEL, pc_, pc_};  // out of range or misaligned
        return out;
    }
    out.next.valid = true;
    out.next.pc    = pc_;
    out.next.pc4   = pc_ + 4;
    out.next.instr = *word;

    // Halt detection: a J/JAL whose resolved target equals the instruction's
    // own address is the "spin-in-place" halt idiom.
    const auto raw_op = static_cast<Opcode>((*word >> 26) & 0x3Fu);
    if (raw_op == Opcode::J || raw_op == Opcode::JAL) {
        const uint32_t tgt   = *word & 0x03FF'FFFFu;
        const uint32_t jaddr = ((pc_ + 4) & 0xF000'0000u) | (tgt << 2);
        if (jaddr == pc_) out.next.is_halt = true;
    }
    return out;
}

// ─── step ────────────────────────────────────────────────────────────────────
StepResult PipelinedCpu::step() {
    ++cycle_;

    // ── Snapshot of the pipeline registers at the start of this cycle ─────────
    // Naming: cur_* = "what the stage sees as its INPUT this cycle".
    const IfId  cur_if  = if_id_;
    const IdEx  cur_id  = id_ex_;
    const ExMem cur_ex  = ex_mem_;
    const MemWb cur_mem = mem_wb_;

    // ── Stages, oldest first ─────────────────────────────────────────────────
    // A trap in MEM squashes EX; a trap or redirect in EX squashes ID and IF; a
    // trap in ID squashes IF. A squashed stage is not run at all. The hazard
    // unit looks only at the pipeline registers, so a load-use stall is still
    // reported in a cycle that also flushes.
    const bool       halted         = write_back(cur_mem);
    const MemOutcome mem            = memory_access(cur_ex);
    const ExOutcome  ex             = mem.trap ? ExOutcome{} : execute(cur_id, cur_ex, cur_mem);
    const bool       stall_load_use = load_use_hazard(cur_id, cur_if);
    const bool       ex_flushes     = mem.trap || ex.trap || ex.redirect;
    const IdOutcome  id =
        (stall_load_use || ex_flushes || !cur_if.valid) ? IdOutcome{} : decode(cur_if);
    const IfOutcome fetched = (stall_load_use || ex_flushes || id.trap) ? IfOutcome{} : fetch();

    // ── Resolve: the oldest stage that traps or redirects wins ───────────────
    std::optional<Trap>     trap = mem.trap ? mem.trap : ex.trap;
    std::optional<uint32_t> redirect;
    if (!trap) {
        // EX survived: apply its CP0 access and control transfer.
        if (ex.mtc0) cp0_.write(ex.mtc0->reg, ex.mtc0->value);
        if (ex.eret) cp0_.eret();
        redirect = ex.redirect;
        if (!redirect) trap = id.trap ? id.trap : fetched.trap;
    }

    StepResult result    = halted ? StepResult::Halt : StepResult::Ok;
    uint32_t   branch_pc = 0;  // target of a 2-stage flush: branch, jump register, or vector
    if (trap) {
        branch_pc = cp0_.raise(trap->code, trap->pc, trap->bad_addr);
        result    = StepResult::Exception;
    } else if (redirect) {
        branch_pc = *redirect;
    }
    const bool flush_from_ex = trap.has_value() || redirect.has_value();  // 2 stages
    const bool flush_from_id = id.jump.has_value();                       // 1 stage (J/JAL)

    // ── Determine next PC and apply flushes ──────────────────────────────────
    // Priority: flush_from_ex (2 stages) > flush_from_id (1 stage) > stall > normal.
    IfId     new_if  = fetched.next;
    IdEx     new_id  = id.next;
    uint32_t next_pc = pc_ + 4;
    if (flush_from_ex) {
        // Branch taken, register jump or trap: discard both the IF and ID results.
        next_pc = branch_pc;
        new_if  = {};
        new_id  = {};
    } else if (flush_from_id) {
        // J/JAL: discard the IF result only (1 bubble). new_id carries the jump
        // into EX, which JAL needs for its link.
        next_pc = *id.jump;
        new_if  = {};
    } else if (stall_load_use) {
        // Hold PC and IF/ID; inject a bubble into ID/EX.
        next_pc = pc_;
        new_if  = cur_if;
        new_id  = {};
    }

    // ── Trace: hazard/stall/flush events and the instruction entering ID ──────
    if (trace_enabled(spdlog::level::trace)) {
        if (stall_load_use)
            trace_log().trace("pl  cyc={:<6} load-use hazard: stall, bubble into ID/EX", cycle_);
        if (flush_from_ex)
            trace_log().trace("pl  cyc={:<6} flush 2 (branch/jump/exception) -> pc={:#010x}",
                              cycle_, branch_pc);
        else if (flush_from_id)
            trace_log().trace("pl  cyc={:<6} flush 1 (j/jal) -> pc={:#010x}", cycle_, next_pc);
        if (new_if.valid) {
            const auto        if_dec = Decoder::decode(new_if.instr);
            const std::string asm_text =
                if_dec ? Disassembler::to_string(*if_dec, new_if.pc) : "<undecodable>";
            trace_log().trace("pl  cyc={:<6} IF pc={:#010x}  {:#010x}  {}", cycle_, new_if.pc,
                              new_if.instr, asm_text);
        }
    }

    // ── Commit ───────────────────────────────────────────────────────────────
    mem_wb_ = mem.next;
    ex_mem_ = ex.next;
    id_ex_  = new_id;
    if_id_  = new_if;
    pc_     = next_pc;

    // ── Update PipelineState for the front ends ──────────────────────────────
    // Reflect what each stage was *doing* this cycle (its input registers).
    ps_.stages[0] = {"IF",      new_if.valid, stall_load_use, flush_from_ex || flush_from_id,
                     new_if.pc, new_if.instr};
    ps_.stages[1] = {"ID", cur_if.valid, stall_load_use, flush_from_ex, cur_if.pc, cur_if.instr};
    ps_.stages[2] = {"EX", cur_id.valid, false, false, cur_id.pc, cur_id.decoded.raw};
    ps_.stages[3] = {"MEM", cur_ex.valid, false, false, cur_ex.pc, cur_ex.raw};
    ps_.stages[4] = {"WB", cur_mem.valid, false, false, cur_mem.pc, cur_mem.raw};

    ps_.fwd_ex_to_ex_a  = ex.forwarded.ex_mem_a;
    ps_.fwd_ex_to_ex_b  = ex.forwarded.ex_mem_b;
    ps_.fwd_mem_to_ex_a = ex.forwarded.mem_wb_a;
    ps_.fwd_mem_to_ex_b = ex.forwarded.mem_wb_b;
    ps_.load_stall      = stall_load_use;
    ps_.branch_flush    = flush_from_ex;
    ps_.retired         = cur_mem.valid;  // WB ran on a real instruction this cycle
    ps_.cycle           = cycle_;

    return result;
}

bool PipelinedCpu::load_program(const std::vector<uint32_t>& words, uint32_t addr) {
    if (!mem_.load_words(addr, words)) return false;
    pc_ = addr;
    return true;
}

void PipelinedCpu::reset(bool clear_memory) {
    regs_.reset();
    cp0_.reset();
    pc_     = 0;
    hi_     = 0;
    lo_     = 0;
    ctrl_   = Control{};
    cycle_  = 0;
    ps_     = {};
    if_id_  = {};
    id_ex_  = {};
    ex_mem_ = {};
    mem_wb_ = {};
    if (clear_memory) mem_.reset();
}

}  // namespace mips
