#pragma once

// ─── mips/processor.h ────────────────────────────────────────────────────────
// The ISA-agnostic interface now lives in <isa/processor.h>. This header:
//   • re-exports isa::IProcessor / StepResult / PipelineState / StageSnapshot
//     into the `mips` namespace so existing callers compile unchanged, and
//   • adds MIPS-specific control derivation and the `IMipsProcessor`
//     sub-interface that exposes coprocessor 0 and the HI/LO registers.
//
// A concrete MIPS core derives from IMipsProcessor; a future RISC-V core derives
// straight from isa::IProcessor (plus its own CSR sub-interface).

#include "isa/processor.h"
#include "mips/cp0.h"
#include "mips/decoder.h"
#include "mips/memory.h"
#include "mips/registers.h"

#include <cstdint>

namespace mips {

// Re-export the ISA-agnostic vocabulary so `mips::IProcessor`, `mips::StepResult`,
// `mips::PipelineState`, and `mips::StageSnapshot` keep resolving for callers that
// only need the generic contract (TUI render helpers, the Qt bridge, tests).
using isa::IProcessor;
using isa::PipelineState;
using isa::StageSnapshot;
using isa::StepResult;

// ─── Control word ─────────────────────────────────────────────────────────────
// Derived purely from opcode+funct (H&H Figure 7.11). Both MIPS implementations
// produce one of these per instruction; the TUI can render it per cycle. This is
// MIPS-specific (note reg_dst, the R-vs-I destination select MIPS needs and
// RISC-V does not), so it stays in the `mips` namespace.
struct Control {
    bool reg_write                              = false;
    bool mem_read                               = false;
    bool mem_write                              = false;
    bool mem_to_reg                             = false;
    bool alu_src                                = false;
    bool reg_dst                                = false;
    bool branch                                 = false;
    bool jump                                   = false;
    enum class Ext : uint8_t { Sign, Zero } ext = Ext::Sign;
};

// ─── Derive control — shared between both CPU implementations ─────────────────
// Declared here, defined in single_cycle_cpu.cpp so pipelined_cpu.cpp can use
// it without duplicating the switch table.
[[nodiscard]] Control derive_control(const DecodedInstr& instr);

// ─── IMipsProcessor ───────────────────────────────────────────────────────────
// The MIPS contract: the ISA-agnostic IProcessor plus the MIPS-only
// architectural state — coprocessor 0 (Status/Cause/EPC/BadVAddr) and the HI/LO
// multiply/divide registers, together with the last committed control word.
class IMipsProcessor : public isa::IProcessor {
public:
    // Control word of the last instruction that committed in WB.
    [[nodiscard]] virtual const Control& last_control() const noexcept = 0;

    // ── Coprocessor 0 ─────────────────────────────────────────────────────────
    // Access to Status, Cause, EPC, BadVAddr, and the last exception code.
    // Valid to read after any step() returns StepResult::Exception.
    [[nodiscard]] virtual const Cp0& cp0() const noexcept = 0;
    [[nodiscard]] virtual Cp0&       cp0() noexcept       = 0;

    // HI and LO registers (destination for MULT/DIV; exposed for GDB register
    // layout even before those instructions are implemented).
    [[nodiscard]] virtual uint32_t hi() const noexcept         = 0;
    [[nodiscard]] virtual uint32_t lo() const noexcept         = 0;
    virtual void                   set_hi(uint32_t v) noexcept = 0;
    virtual void                   set_lo(uint32_t v) noexcept = 0;
};

}  // namespace mips
