#include "mips/pipelined_cpu.h"

#include <vector>

#include "mips/alu.h"
#include "mips/decoder.h"

// ─── pipelined_cpu.cpp ───────────────────────────────────────────────────────
// Each step() models one clock cycle.  The five stages are processed in the
// order WB → MEM → EX → ID → IF so that register-file writes (WB) are
// visible to register-file reads (ID) within the same cycle — the "first-
// half write / second-half read" convention from H&H §8.4.
//
// All new pipeline-register values are computed from the *current* (pre-commit)
// state and committed atomically at the end of the cycle.  This keeps each
// stage's view of the world consistent for the duration of a single step().

namespace mips {

PipelinedCpu::PipelinedCpu(std::size_t mem_bytes) : mem_(mem_bytes) {}

// ─────────────────────────────────────────────────────────────────────────────
StepResult PipelinedCpu::step() {
    ++cycle_;

    // ── Snapshot of the pipeline registers at the start of this cycle ─────────
    // Naming: cur_* = "what the stage sees as its INPUT this cycle".
    const IfId  cur_if  = if_id_;
    const IdEx  cur_id  = id_ex_;
    const ExMem cur_ex  = ex_mem_;
    const MemWb cur_mem = mem_wb_;

    // New values that will be committed at the end of the cycle.
    IfId  new_if{};
    IdEx  new_id{};
    ExMem new_ex{};
    MemWb new_mem{};

    // Hazard / flush flags set by various stages.
    bool stall_load_use = false;
    bool flush_from_ex  = false;  // 2-stage flush (branch, JR, JALR, exceptions)
    bool flush_from_id  = false;  // 1-stage flush (J, JAL)
    // Use SEPARATE target variables: EX and ID both run in the same step() call
    // and both may set a redirect.  flush_from_ex wins (higher priority), so we
    // must not let ID's write overwrite EX's target in a shared variable.
    uint32_t   branch_pc = 0;  // target set by EX (branch / JR / JALR / exception)
    uint32_t   jump_pc   = 0;  // target set by ID (J / JAL)
    StepResult result    = StepResult::Ok;

    // Forwarding indicators for PipelineState
    bool fwd_ex_a = false, fwd_ex_b = false;
    bool fwd_mem_a = false, fwd_mem_b = false;

    // ── WB stage ─────────────────────────────────────────────────────────────
    // Process first so ID (below) reads the updated register file value.
    if (cur_mem.valid) {
        ctrl_ = cur_mem.ctrl;
        if (cur_mem.ctrl.reg_write && cur_mem.write_reg != 0) {
            const uint32_t wval = cur_mem.ctrl.mem_to_reg ? cur_mem.mem_val : cur_mem.alu_val;
            regs_.write(cur_mem.write_reg, wval);
        }
        if (cur_mem.is_halt) result = StepResult::Halt;
    }

    // ── MEM stage ────────────────────────────────────────────────────────────
    if (cur_ex.valid) {
        new_mem.valid     = true;
        new_mem.pc        = cur_ex.pc;
        new_mem.ctrl      = cur_ex.ctrl;
        new_mem.alu_val   = cur_ex.alu.value;
        new_mem.write_reg = cur_ex.write_reg;
        new_mem.is_halt   = cur_ex.is_halt;

        if (cur_ex.ctrl.mem_read) {
            const uint32_t          addr = cur_ex.alu.value;
            std::optional<uint32_t> loaded;
            switch (cur_ex.opcode) {
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
                // AdEL exception: flush EX, ID, IF and vector to exception handler.
                branch_pc     = cp0_.raise(ExceptionCode::AdEL, cur_ex.pc, addr);
                flush_from_ex = true;
                new_mem       = {};
                result        = StepResult::Exception;
            } else {
                new_mem.mem_val = *loaded;
            }
        }
        if (cur_ex.ctrl.mem_write && result != StepResult::Exception) {
            const uint32_t addr = cur_ex.alu.value;
            if (!mem_.write_word(addr, cur_ex.rt_val)) {
                branch_pc     = cp0_.raise(ExceptionCode::AdES, cur_ex.pc, addr);
                flush_from_ex = true;
                new_mem       = {};
                result        = StepResult::Exception;
            }
        }
    }

    // ── EX stage ─────────────────────────────────────────────────────────────
    if (cur_id.valid) {
        // ── Forwarding unit ──────────────────────────────────────────────────
        // Priority: EX/MEM > MEM/WB (H&H Figure 8.38).
        // Note: cur_mem is the MEM/WB register BEFORE WB wrote to the register
        // file, so its value is still needed for the MEM/WB → EX path.
        uint32_t fwd_a = cur_id.rs_val;
        uint32_t fwd_b = cur_id.rt_val;

        // EX/MEM → EX.
        // Exclude loads (mem_read): the EX/MEM register holds the *address*
        // at this point, not the loaded word (the memory access happens in the
        // MEM stage this same cycle). A load's result can only be forwarded one
        // cycle later from MEM/WB — which is exactly why the load-use hazard
        // unit inserts a bubble. Guarding here keeps this correct even if the
        // hazard logic is ever changed (H&H Figure 8.38: ForwardA/B exclude
        // the load-result path).
        if (cur_ex.valid && cur_ex.ctrl.reg_write && !cur_ex.ctrl.mem_read &&
            cur_ex.write_reg != 0) {
            if (cur_ex.write_reg == cur_id.rs) {
                fwd_a    = cur_ex.alu.value;
                fwd_ex_a = true;
            }
            if (cur_ex.write_reg == cur_id.rt) {
                fwd_b    = cur_ex.alu.value;
                fwd_ex_b = true;
            }
        }

        // MEM/WB → EX (use the value WB *would* write, whether ALU or load data)
        const uint32_t wb_val = cur_mem.ctrl.mem_to_reg ? cur_mem.mem_val : cur_mem.alu_val;
        if (cur_mem.valid && cur_mem.ctrl.reg_write && cur_mem.write_reg != 0) {
            if (cur_mem.write_reg == cur_id.rs && !fwd_ex_a) {
                fwd_a     = wb_val;
                fwd_mem_a = true;
            }
            if (cur_mem.write_reg == cur_id.rt && !fwd_ex_b) {
                fwd_b     = wb_val;
                fwd_mem_b = true;
            }
        }

        const DecodedInstr& dec = cur_id.decoded;

        // ── J / JAL: jump was resolved in ID; EX just handles JAL link ───────
        if (dec.format == InstrFormat::J) {
            new_ex.valid   = true;
            new_ex.pc      = cur_id.pc;
            new_ex.ctrl    = cur_id.ctrl;
            new_ex.is_halt = cur_id.is_halt;
            new_ex.opcode  = dec.opcode;
            if (dec.opcode == Opcode::JAL) {
                new_ex.alu.value = cur_id.pc4;  // return address
                new_ex.write_reg = 31;          // $ra
            }
            // J: ctrl.reg_write is already false; write_reg stays 0.
        } else if (dec.format == InstrFormat::I &&
                   (dec.opcode == Opcode::BEQ || dec.opcode == Opcode::BNE)) {
            // ── BEQ / BNE: branch resolved in EX, 2-cycle flush ───────────
            const AluResult cmp   = Alu::execute(AluOp::SUBU, fwd_a, fwd_b);
            const bool      taken = (dec.opcode == Opcode::BEQ) ? cmp.zero : !cmp.zero;
            if (taken) {
                const int32_t off = Decoder::sign_extend(dec.i().imm);
                branch_pc         = cur_id.pc4 + (static_cast<uint32_t>(off) << 2);
                flush_from_ex     = true;
            }
            // No writeback for branch instructions; new_ex stays invalid.
        } else if (dec.format == InstrFormat::R &&
                   (dec.r().funct == FunctCode::JR || dec.r().funct == FunctCode::JALR)) {
            // ── JR / JALR: register jump resolved in EX, 2-cycle flush ────
            branch_pc     = fwd_a;  // rs, potentially forwarded
            flush_from_ex = true;
            if (dec.r().funct == FunctCode::JALR) {
                new_ex.valid     = true;
                new_ex.pc        = cur_id.pc;
                new_ex.ctrl      = cur_id.ctrl;
                new_ex.alu.value = cur_id.pc4;  // return address
                new_ex.write_reg = dec.r().rd;
                new_ex.is_halt   = cur_id.is_halt;
            }
            // JR: no writeback; new_ex stays invalid.
        } else if (dec.format == InstrFormat::R &&
                   (dec.r().funct == FunctCode::SYSCALL || dec.r().funct == FunctCode::BREAK)) {
            // ── SYSCALL / BREAK: raise CP0 exception, flush 2 stages ──────
            const ExceptionCode code =
                (dec.r().funct == FunctCode::SYSCALL) ? ExceptionCode::Sys : ExceptionCode::Bp;
            branch_pc     = cp0_.raise(code, cur_id.pc);
            flush_from_ex = true;
            result        = StepResult::Exception;
            // new_ex stays invalid — the exception handler gets a clean pipeline.
        } else if (dec.opcode == Opcode::COP0) {
            // ── COP0: MFC0 / MTC0 / ERET — execute in EX ─────────────────
            const uint8_t sub = dec.r().rs;
            if (sub == 0x00) {
                // MFC0: read CP0 register, write to GPR in WB via alu_val path.
                new_ex.valid          = true;
                new_ex.pc             = cur_id.pc;
                new_ex.ctrl           = cur_id.ctrl;
                new_ex.ctrl.reg_write = true;
                new_ex.alu.value      = cp0_.read(dec.r().rd);
                new_ex.write_reg      = dec.r().rt;
                new_ex.opcode         = dec.opcode;
            } else if (sub == 0x04) {
                // MTC0: write forwarded GPR value to CP0 register.
                cp0_.write(dec.r().rd, fwd_b);
                // No pipeline writeback needed.
            } else if (sub == 0x10 && dec.r().funct == FunctCode::ERET) {
                // ERET: clear EXL, jump to EPC — treated as a register jump.
                branch_pc     = cp0_.eret();
                flush_from_ex = true;
            }
            // Unknown COP0 sub-ops are silently ignored (EHB, etc.).
        } else {
            // ── Regular ALU instruction ────────────────────────────────────
            const auto aluop = Alu::control(dec);
            if (!aluop) {
                // Unrecognised instruction in EX: raise RI exception.
                branch_pc     = cp0_.raise(ExceptionCode::RI, cur_id.pc);
                flush_from_ex = true;
                result        = StepResult::Exception;
            } else {
                uint8_t shamt = (dec.format == InstrFormat::R) ? dec.r().shamt : 0;
                if (dec.format == InstrFormat::R) {
                    const auto f = dec.r().funct;
                    if (f == FunctCode::SLLV || f == FunctCode::SRLV)
                        shamt = static_cast<uint8_t>(fwd_a & 0x1Fu);
                }

                // ALU B: register value or sign/zero-extended immediate.
                uint32_t alu_b = fwd_b;
                if (cur_id.ctrl.alu_src && dec.format == InstrFormat::I) {
                    alu_b = (cur_id.ctrl.ext == Control::Ext::Sign)
                                ? static_cast<uint32_t>(Decoder::sign_extend(dec.i().imm))
                                : static_cast<uint32_t>(dec.i().imm);
                }

                const AluResult alu_res = Alu::execute(*aluop, fwd_a, alu_b, shamt);

                // Signed overflow on ADD/SUB/ADDI raises Ov (ADDU/SUBU/ADDIU do not).
                if (alu_res.overflow) {
                    const auto funct =
                        (dec.format == InstrFormat::R) ? dec.r().funct : FunctCode::SLL;
                    const bool is_signed_op =
                        (funct == FunctCode::ADD || funct == FunctCode::SUB) ||
                        (dec.opcode == Opcode::ADDI);
                    if (is_signed_op) {
                        branch_pc     = cp0_.raise(ExceptionCode::Ov, cur_id.pc);
                        flush_from_ex = true;
                        result        = StepResult::Exception;
                        // Skip normal writeback.
                        goto ex_done;
                    }
                }

                {
                    // Destination register: rd (R-type), rt (I-type).
                    uint8_t write_reg = 0;
                    if (cur_id.ctrl.reg_dst && dec.format == InstrFormat::R)
                        write_reg = dec.r().rd;
                    else if (dec.format == InstrFormat::I)
                        write_reg = dec.i().rt;

                    new_ex.valid     = true;
                    new_ex.pc        = cur_id.pc;
                    new_ex.ctrl      = cur_id.ctrl;
                    new_ex.alu       = alu_res;
                    new_ex.rt_val    = fwd_b;  // forwarded rt for SW
                    new_ex.write_reg = write_reg;
                    new_ex.opcode    = dec.opcode;
                    new_ex.is_halt   = cur_id.is_halt;
                }
            }
        }
    ex_done: {}
    }

    // ── ID stage ─────────────────────────────────────────────────────────────

    // ── Hazard-detection unit ────────────────────────────────────────────────
    // If the instruction now in EX (cur_id) is a load AND the instruction now
    // in ID (cur_if) actually *reads* the register the load writes, stall one
    // cycle (H&H §8.5, Figure 8.34). The load's destination is its I-format rt.
    //
    // The source registers are determined from the decoded form of the ID-stage
    // instruction, NOT from raw bit positions: J/JAL carry target bits where
    // rs/rt would be, LUI ignores rs, ALU-immediate/loads do not read rt, and
    // shifts do not read rs. Reading raw bits would manufacture phantom stalls
    // that corrupt the cycle/CPI telemetry the visualiser exists to show.
    if (cur_id.valid && cur_id.ctrl.mem_read && cur_if.valid) {
        int src_a = -1, src_b = -1;  // -1 ⇒ "this instruction reads no such reg"
        if (const auto if_dec = Decoder::decode(cur_if.instr)) {
            const DecodedInstr& d = *if_dec;
            switch (d.format) {
            case InstrFormat::R:
                switch (d.r().funct) {
                case FunctCode::SLL:
                case FunctCode::SRL:
                case FunctCode::SRA:
                    src_b = d.r().rt;  // shift: rt only
                    break;
                case FunctCode::JR:
                case FunctCode::JALR:
                    src_a = d.r().rs;  // reg-jump: rs only
                    break;
                default:
                    src_a = d.r().rs;
                    src_b = d.r().rt;  // arith: rs, rt
                    break;
                }
                break;
            case InstrFormat::I:
                switch (d.opcode) {
                case Opcode::LUI:
                    break;  // reads nothing
                case Opcode::SW:
                case Opcode::BEQ:
                case Opcode::BNE:
                    src_a = d.i().rs;
                    src_b = d.i().rt;  // base+data / compare
                    break;
                default:
                    src_a = d.i().rs;  // load/alu-imm: rs only
                    break;
                }
                break;
            default:
                break;  // J/JAL: reads nothing
            }
        }
        const int load_dst = static_cast<int>(cur_id.rt);  // $zero is never a dependency
        if (load_dst != 0 && (load_dst == src_a || load_dst == src_b)) stall_load_use = true;
    }

    if (!stall_load_use && cur_if.valid) {
        const auto dec_opt = Decoder::decode(cur_if.instr);
        if (!dec_opt) {
            // Unrecognised instruction: raise Reserved Instruction exception.
            branch_pc     = cp0_.raise(ExceptionCode::RI, cur_if.pc);
            flush_from_ex = true;
            result        = StepResult::Exception;
            goto id_done;
        }
        const DecodedInstr& dec = *dec_opt;

        // Register indices needed for hazard detection next cycle and for
        // forwarding in EX this cycle.
        uint8_t rs = 0, rt = 0;
        if (dec.format == InstrFormat::R) {
            rs = dec.r().rs;
            rt = dec.r().rt;
        } else if (dec.format == InstrFormat::I) {
            rs = dec.i().rs;
            rt = dec.i().rt;
        }

        // Register-file read happens AFTER WB wrote (WB processed above).
        new_id.valid   = true;
        new_id.pc      = cur_if.pc;
        new_id.pc4     = cur_if.pc4;
        new_id.ctrl    = derive_control(dec);
        new_id.decoded = dec;
        new_id.rs_val  = regs_.read(rs);
        new_id.rt_val  = regs_.read(rt);
        new_id.rs      = rs;
        new_id.rt      = rt;
        new_id.is_halt = cur_if.is_halt;

        // J / JAL: jump target is computable from instruction bits alone —
        // resolve here (ID stage) for a 1-cycle penalty, per H&H Figure 8.30.
        if (dec.format == InstrFormat::J) {
            jump_pc       = (cur_if.pc4 & 0xF000'0000u) | (dec.j().target << 2);
            flush_from_id = true;
        }
    }
id_done: {}

    // ── IF stage ─────────────────────────────────────────────────────────────
    if (!stall_load_use) {
        if (const auto fetched = mem_.read_word(pc_)) {
            new_if.valid = true;
            new_if.pc    = pc_;
            new_if.pc4   = pc_ + 4;
            new_if.instr = *fetched;

            // Halt detection: a J/JAL whose resolved target equals the
            // instruction's own address is the "spin-in-place" halt idiom.
            const auto raw_op = static_cast<Opcode>((*fetched >> 26) & 0x3Fu);
            if (raw_op == Opcode::J || raw_op == Opcode::JAL) {
                const uint32_t tgt   = *fetched & 0x03FF'FFFFu;
                const uint32_t jaddr = ((pc_ + 4) & 0xF000'0000u) | (tgt << 2);
                if (jaddr == pc_) new_if.is_halt = true;
            }
        } else {
            // OOB or misaligned fetch → AdEL exception.
            branch_pc     = cp0_.raise(ExceptionCode::AdEL, pc_, pc_);
            flush_from_ex = true;
            result        = StepResult::Exception;
        }
    }

    // ── Determine next PC and apply flushes ──────────────────────────────────
    // Priority: flush_from_ex (2 stages) > flush_from_id (1 stage) > stall > normal.

    uint32_t next_pc = pc_ + 4;  // normal advance unless a flush/stall overrides

    if (flush_from_ex) {
        // Branch taken or JR/JALR: discard both the IF and ID results.
        next_pc = branch_pc;
        new_if  = {};  // flush IF output
        new_id  = {};  // flush ID output (J in ID also discarded if present)
    } else if (flush_from_id) {
        // J/JAL: discard the IF result only (1 bubble).
        next_pc = jump_pc;
        new_if  = {};  // flush IF output
        // new_id carries the J/JAL instruction into EX (needed for JAL link).
    } else if (stall_load_use) {
        // Hold PC and IF/ID; inject a bubble into ID/EX.
        next_pc = pc_;     // do not advance
        new_if  = cur_if;  // hold (overwrite the bubble we may have computed)
        new_id  = {};      // bubble
    }

    // ── Commit ───────────────────────────────────────────────────────────────
    mem_wb_ = new_mem;
    ex_mem_ = new_ex;
    id_ex_  = new_id;
    if_id_  = new_if;
    pc_     = next_pc;

    // ── Update PipelineState for the TUI ─────────────────────────────────────
    // Reflect what each stage was *doing* this cycle (its input registers).
    ps_.stages[0] = {"IF",      new_if.valid, stall_load_use, flush_from_ex || flush_from_id,
                     new_if.pc, new_if.instr};
    ps_.stages[1] = {"ID", cur_if.valid, stall_load_use, flush_from_ex, cur_if.pc, cur_if.instr};
    ps_.stages[2] = {"EX", cur_id.valid, false, false, cur_id.pc, cur_id.decoded.raw};
    ps_.stages[3] = {"MEM", cur_ex.valid, false, false, cur_ex.pc, 0};
    ps_.stages[4] = {"WB", cur_mem.valid, false, false, cur_mem.pc, 0};

    ps_.fwd_ex_to_ex_a  = fwd_ex_a;
    ps_.fwd_ex_to_ex_b  = fwd_ex_b;
    ps_.fwd_mem_to_ex_a = fwd_mem_a;
    ps_.fwd_mem_to_ex_b = fwd_mem_b;
    ps_.load_stall      = stall_load_use;
    ps_.branch_flush    = flush_from_ex;
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
