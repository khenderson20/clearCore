#include "mips/pipeline_units.h"

namespace mips {

SourceRegisters source_registers(const DecodedInstr& d) {
    SourceRegisters src;
    switch (d.format) {
    case InstrFormat::R:
        switch (d.r().funct) {
        case FunctCode::SLL:
        case FunctCode::SRL:
        case FunctCode::SRA:
            src.b = d.r().rt;  // shift: rt only
            break;
        case FunctCode::JR:
        case FunctCode::JALR:
            src.a = d.r().rs;  // register jump: rs only
            break;
        default:
            src.a = d.r().rs;
            src.b = d.r().rt;  // arithmetic: rs and rt
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
            src.a = d.i().rs;
            src.b = d.i().rt;  // base and data / both compare operands
            break;
        default:
            src.a = d.i().rs;  // load or ALU-immediate: rs only
            break;
        }
        break;
    default:
        break;  // J/JAL: reads nothing
    }
    // COP0 decodes as R-format, but rs is the sub-op selector (MFC0/MTC0/ERET),
    // not a register, and MFC0 writes rt rather than reading it.
    if (d.opcode == Opcode::COP0) {
        src.a = -1;
        src.b = (d.r().rs == 0x04) ? static_cast<int>(d.r().rt) : -1;
    }
    return src;
}

bool load_use_hazard(const IdEx& in_ex, const IfId& in_id) {
    if (!in_ex.valid || !in_ex.ctrl.mem_read || !in_id.valid || in_id.fetch_fault) return false;
    const auto decoded = Decoder::decode(in_id.instr);
    if (!decoded) return false;
    const SourceRegisters src      = source_registers(*decoded);
    const int             load_dst = in_ex.rt;  // a load writes its I-format rt
    return load_dst != 0 && (load_dst == src.a || load_dst == src.b);
}

ExOperands forward_operands(const IdEx& in_ex, const ExMem& ex_mem, const MemWb& mem_wb) noexcept {
    ExOperands ops{in_ex.rs_val, in_ex.rt_val, {}};

    if (ex_mem.valid && ex_mem.ctrl.reg_write && !ex_mem.ctrl.mem_read && ex_mem.write_reg != 0) {
        if (ex_mem.write_reg == in_ex.rs) {
            ops.a              = ex_mem.alu.value;
            ops.paths.ex_mem_a = true;
        }
        if (ex_mem.write_reg == in_ex.rt) {
            ops.b              = ex_mem.alu.value;
            ops.paths.ex_mem_b = true;
        }
    }

    // MEM/WB holds the value WB is writing this cycle, whether ALU result or load data.
    if (mem_wb.valid && mem_wb.ctrl.reg_write && mem_wb.write_reg != 0) {
        const uint32_t wb_val = mem_wb.ctrl.mem_to_reg ? mem_wb.mem_val : mem_wb.alu_val;
        if (mem_wb.write_reg == in_ex.rs && !ops.paths.ex_mem_a) {
            ops.a              = wb_val;
            ops.paths.mem_wb_a = true;
        }
        if (mem_wb.write_reg == in_ex.rt && !ops.paths.ex_mem_b) {
            ops.b              = wb_val;
            ops.paths.mem_wb_b = true;
        }
    }
    return ops;
}

}  // namespace mips
