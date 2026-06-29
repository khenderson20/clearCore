#include "mips/cpu.h"

namespace mips {

Cpu::Cpu(std::size_t mem_bytes) : mem_(mem_bytes) {}

// ─── Control unit ─────────────────────────────────────────────────────────────
Control Cpu::control(const DecodedInstr& instr) {
    Control c;
    switch (instr.format) {
        case InstrFormat::R: {
            switch (instr.r().funct) {
                case FunctCode::JR:                       // register jump
                    return c;                             // no writeback, no mem
                case FunctCode::JALR:                     // register jump + link
                    c.reg_write = true;                   // dest is rd
                    return c;
                default:                                  // ALU result → rd
                    c.reg_write = true;
                    c.reg_dst   = true;
                    return c;
            }
        }
        case InstrFormat::I: {
            switch (instr.opcode) {
                case Opcode::ADDI:  case Opcode::ADDIU:
                case Opcode::SLTI:  case Opcode::SLTIU:    // sign-extended imm → rt
                    c.reg_write = true; c.alu_src = true;
                    c.ext = Control::Ext::Sign;
                    return c;
                case Opcode::ANDI:  case Opcode::ORI:
                case Opcode::XORI:                         // zero-extended imm → rt
                    c.reg_write = true; c.alu_src = true;
                    c.ext = Control::Ext::Zero;
                    return c;
                case Opcode::LUI:                          // imm into upper half
                    c.reg_write = true; c.alu_src = true;
                    c.ext = Control::Ext::Zero;
                    return c;
                case Opcode::LW:    case Opcode::LBU:
                case Opcode::LHU:                          // load: mem → rt
                    c.reg_write = true; c.mem_read = true;
                    c.mem_to_reg = true; c.alu_src = true;
                    c.ext = Control::Ext::Sign;
                    return c;
                case Opcode::SW:                           // store: rt → mem
                    c.mem_write = true; c.alu_src = true;
                    c.ext = Control::Ext::Sign;
                    return c;
                case Opcode::BEQ:   case Opcode::BNE:       // compare rs, rt
                    c.branch = true;
                    c.ext = Control::Ext::Sign;
                    return c;
                default:
                    return c;
            }
        }
        case InstrFormat::J: {
            c.jump = true;
            if (instr.opcode == Opcode::JAL) c.reg_write = true;   // link → $ra
            return c;
        }
        default:
            return c;
    }
}

// ─── R-type execute + writeback ───────────────────────────────────────────────
bool Cpu::exec_rtype(const DecodedInstr& d, uint32_t pc_plus_4, uint32_t& next_pc) {
    const RFields&  r = d.r();
    const FunctCode f = r.funct;

    // Register jumps redirect the PC instead of writing an ALU result.
    if (f == FunctCode::JR) {
        next_pc = regs_.read(r.rs);
        return true;
    }
    if (f == FunctCode::JALR) {
        const uint32_t target = regs_.read(r.rs);
        regs_.write(r.rd, pc_plus_4);          // link before redirect
        next_pc = target;
        return true;
    }

    const auto op = Alu::control(d);
    if (!op) return false;                     // reserved funct → fault

    const uint32_t a = regs_.read(r.rs);
    const uint32_t b = regs_.read(r.rt);

    // Variable shifts take their amount from rs[4:0]; fixed shifts from shamt.
    uint8_t shamt = r.shamt;
    if (f == FunctCode::SLLV || f == FunctCode::SRLV) {
        shamt = static_cast<uint8_t>(a & 0x1Fu);
    }

    // execute() shifts operand b for shifts and uses (a, b) for everything else.
    const AluResult res = Alu::execute(*op, a, b, shamt);
    regs_.write(r.rd, res.value);
    return true;
}

// ─── I-type execute + memory + writeback ──────────────────────────────────────
bool Cpu::exec_itype(const DecodedInstr& d, uint32_t pc_plus_4, uint32_t& next_pc) {
    const IFields& i  = d.i();
    const Opcode   op = d.opcode;

    const uint32_t rs_val = regs_.read(i.rs);
    const uint32_t rt_val = regs_.read(i.rt);

    // Branches resolve here and bypass the normal writeback path.
    if (op == Opcode::BEQ || op == Opcode::BNE) {
        const auto aluop = Alu::control(d);              // SUBU → zero flag
        if (!aluop) return false;
        const AluResult res = Alu::execute(*aluop, rs_val, rt_val);
        const bool taken = (op == Opcode::BEQ) ? res.zero : !res.zero;
        if (taken) {
            const int32_t off = Decoder::sign_extend(i.imm);
            next_pc = pc_plus_4 + (static_cast<uint32_t>(off) << 2);
        }
        return true;
    }

    // Second ALU operand: immediate, extended per the control word.
    const uint32_t imm = (ctrl_.ext == Control::Ext::Sign)
        ? static_cast<uint32_t>(Decoder::sign_extend(i.imm))
        : static_cast<uint32_t>(i.imm);                 // zero-extend

    const auto aluop = Alu::control(d);
    if (!aluop) return false;
    const AluResult res = Alu::execute(*aluop, rs_val, imm);

    // Loads: ALU result is the effective address; zero-extended unsigned loads.
    if (op == Opcode::LW || op == Opcode::LBU || op == Opcode::LHU) {
        const uint32_t addr = res.value;
        std::optional<uint32_t> loaded;
        switch (op) {
            case Opcode::LW:  loaded = mem_.read_word(addr); break;
            case Opcode::LBU: if (auto v = mem_.read_byte(addr)) loaded = *v; break;
            case Opcode::LHU: if (auto v = mem_.read_half(addr)) loaded = *v; break;
            default: break;
        }
        if (!loaded) return false;                      // bad/misaligned → fault
        regs_.write(i.rt, *loaded);
        return true;
    }

    // Store: write rt to the effective address.
    if (op == Opcode::SW) {
        return mem_.write_word(res.value, rt_val);
    }

    // Remaining I-types (ADDI/ANDI/LUI/...) write the ALU result to rt.
    regs_.write(i.rt, res.value);
    return true;
}

// ─── One datapath tick ────────────────────────────────────────────────────────
StepResult Cpu::step() {
    const uint32_t cur_pc = pc_;

    // Fetch
    const auto fetched = mem_.read_word(cur_pc);
    if (!fetched) return StepResult::Fault;
    const uint32_t pc_plus_4 = cur_pc + 4;

    // Decode
    const auto decoded = Decoder::decode(*fetched);
    if (!decoded) return StepResult::Fault;
    const DecodedInstr& d = *decoded;

    // Control
    ctrl_ = control(d);

    uint32_t next_pc = pc_plus_4;     // sequential default; jumps/branches override
    bool ok = true;

    switch (d.format) {
        case InstrFormat::J:
            if (d.opcode == Opcode::JAL) regs_.write(kRa, pc_plus_4);   // link
            next_pc = (pc_plus_4 & 0xF000'0000u) | (d.j().target << 2);
            break;
        case InstrFormat::R:
            ok = exec_rtype(d, pc_plus_4, next_pc);
            break;
        case InstrFormat::I:
            ok = exec_itype(d, pc_plus_4, next_pc);
            break;
        default:
            return StepResult::Fault;
    }

    if (!ok) return StepResult::Fault;
    pc_ = next_pc;

    // Halt convention: an instruction that jumps/branches to itself (the
    // `loop: j loop` spin idiom, or `jr $ra` returning to here) is a deliberate
    // stop. Sequential instructions advance to pc+4, so this never false-fires.
    return (next_pc == cur_pc) ? StepResult::Halt : StepResult::Ok;
}

StepResult Cpu::run(std::size_t max_steps) {
    for (std::size_t i = 0; i < max_steps; ++i) {
        const StepResult r = step();
        if (r != StepResult::Ok) return r;
    }
    return StepResult::Ok;     // budget exhausted — likely a runaway program
}

bool Cpu::load_program(const std::vector<uint32_t>& words, uint32_t addr) {
    if (!mem_.load_words(addr, words)) return false;
    pc_ = addr;
    return true;
}

void Cpu::reset(bool clear_memory) {
    regs_.reset();
    pc_   = 0;
    ctrl_ = Control{};
    if (clear_memory) mem_.reset();
}

} // namespace mips