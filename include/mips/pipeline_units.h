#pragma once

// ─── pipeline_units.h ────────────────────────────────────────────────────────
// The two combinational blocks beside the 5-stage datapath — the hazard-
// detection unit (H&H §8.5, Figure 8.34) and the forwarding unit (Figure 8.38) —
// as pure functions of the pipeline registers, so they can be tested without
// stepping a CPU.

#include "mips/decoder.h"
#include "mips/pipeline_regs.h"

#include <cstdint>

namespace mips {

// The register indices an instruction reads; -1 means "no such operand".
struct SourceRegisters {
    int a = -1;  // first source (rs position)
    int b = -1;  // second source (rt position)
};

// Determined from the decoded form, not from raw bit positions: J/JAL carry
// target bits where rs/rt would be, LUI ignores rs, ALU-immediates and loads do
// not read rt, shifts do not read rs, and COP0's rs field selects the
// sub-operation (only MTC0 reads a GPR, its rt). Reading raw bits would
// manufacture phantom stalls that corrupt the cycle and CPI telemetry.
[[nodiscard]] SourceRegisters source_registers(const DecodedInstr& d);

// Write-register select (the RegDst mux plus JAL's hard-wired $ra): the GPR the
// instruction writes in WB, or -1 when it writes none. MFC0 writes rt; MTC0,
// ERET, stores, branches, J, JR, SYSCALL and BREAK write no GPR. A write to
// $zero is reported as 0, because the hardware still selects it.
[[nodiscard]] int destination_register(const DecodedInstr& d);

// Hazard-detection unit: true when the load now in EX writes a register that
// the instruction now in ID reads, so ID must wait one cycle.
[[nodiscard]] bool load_use_hazard(const IdEx& in_ex, const IfId& in_id);

// Which forwarding paths supplied the operands of the instruction in EX.
struct ForwardingPaths {
    bool ex_mem_a = false;  // EX/MEM → A (rs)
    bool ex_mem_b = false;  // EX/MEM → B (rt)
    bool mem_wb_a = false;  // MEM/WB → A
    bool mem_wb_b = false;  // MEM/WB → B
};

struct ExOperands {
    uint32_t        a = 0;  // rs value the instruction in EX must use
    uint32_t        b = 0;  // rt value the instruction in EX must use
    ForwardingPaths paths{};
};

// Forwarding unit: the operands for the instruction in EX. EX/MEM has priority
// over MEM/WB because it holds the younger (more recent) result. A load in
// EX/MEM cannot forward — it holds the address, not the loaded word, which is
// why the hazard unit stalls instead.
[[nodiscard]] ExOperands forward_operands(const IdEx& in_ex, const ExMem& ex_mem,
                                          const MemWb& mem_wb) noexcept;

}  // namespace mips
