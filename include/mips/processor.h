#pragma once

// ─── processor.h ─────────────────────────────────────────────────────────────
// Abstract processor interface, modelled on the pluggable-backend pattern used
// by Ripes (Petersen, 2021) and DrMIPS (Nova et al., 2013).
//
// The TUI visualiser (Stage 2) and tests talk exclusively to IProcessor.
// Concrete implementations — SingleCycleCpu and PipelinedCpu — slot in without
// touching caller code.  The key design constraints that flow from the papers:
//
//   1. Observable pipeline state is part of the interface (DrMIPS exposes the
//      datapath graphically; WebRISC-V exposes cycle-by-cycle stage contents).
//   2. step() advances exactly one clock cycle, regardless of implementation.
//   3. run() is a default loop over step() so callers never need to change it.
//   4. Forwarding and stall flags are first-class so the Stage 5 TUI can draw
//      bypass wires and bubble indicators (Arches, Haydel et al. 2025).

#include "mips/decoder.h"
#include "mips/memory.h"
#include "mips/registers.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mips {

// ─── Control word ─────────────────────────────────────────────────────────────
// Derived purely from opcode+funct (H&H Figure 7.11). Both implementations
// produce one of these per instruction; the TUI can render it per cycle.
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

// ─── StepResult ───────────────────────────────────────────────────────────────
enum class StepResult : uint8_t {
    Ok,     // instruction executed; PC advanced
    Fault,  // undecodable instruction or bad/misaligned memory access
    Halt,   // self-targeting jump retired from WB (spin-in-place idiom)
};

// ─── Pipeline visualisation state ─────────────────────────────────────────────
// Filled by every implementation; SingleCycleCpu leaves all but EX empty.
// Directly models the per-stage colour coding in Ripes and WebRISC-V, and the
// forwarding-wire annotations from Arches (Haydel et al., 2025).

struct StageSnapshot {
    const char* name    = "";     // "IF", "ID", "EX", "MEM", "WB"
    bool        valid   = false;  // real instruction present
    bool        stalled = false;  // bubble from a load-use stall
    bool        flushed = false;  // bubble from a branch/jump flush
    uint32_t    pc      = 0;      // PC of the instruction in this stage
    uint32_t    raw     = 0;      // raw 32-bit word (for mnemonic display)
};

struct PipelineState {
    // Index 0 = IF, 1 = ID, 2 = EX, 3 = MEM, 4 = WB
    std::array<StageSnapshot, 5> stages{};

    // Forwarding indicators (Stage 5 bypass-wire visualisation)
    bool fwd_ex_to_ex_a  = false;  // EX/MEM → EX input A
    bool fwd_ex_to_ex_b  = false;
    bool fwd_mem_to_ex_a = false;  // MEM/WB → EX input A
    bool fwd_mem_to_ex_b = false;

    // Hazard indicators
    bool load_stall   = false;
    bool branch_flush = false;

    std::size_t cycle = 0;
};

// ─── IProcessor ───────────────────────────────────────────────────────────────
class IProcessor {
public:
    virtual ~IProcessor() = default;

    // Load a flat array of 32-bit instruction words at `addr`, reset the PC.
    [[nodiscard]] virtual bool load_program(const std::vector<uint32_t>& words,
                                            uint32_t                     addr = 0) = 0;

    // Advance exactly one clock cycle (or one instruction for single-cycle).
    [[nodiscard]] virtual StepResult step() = 0;

    // Step until Fault/Halt or the step budget is exhausted.
    // Default implementation simply loops over step(); concrete classes may
    // override for performance, but the semantics must be identical.
    virtual StepResult run(std::size_t max_steps = 100'000) {
        for (std::size_t i = 0; i < max_steps; ++i) {
            const StepResult r = step();
            if (r != StepResult::Ok) return r;
        }
        return StepResult::Ok;
    }

    // Reset registers, PC, and control snapshot.
    // Pass clear_memory=true to also wipe the address space.
    virtual void reset(bool clear_memory = false) = 0;

    // ── Accessors ──────────────────────────────────────────────────────────────

    [[nodiscard]] virtual uint32_t pc() const noexcept       = 0;
    virtual void                   set_pc(uint32_t) noexcept = 0;

    [[nodiscard]] virtual const RegisterFile& regs() const noexcept = 0;
    [[nodiscard]] virtual RegisterFile&       regs() noexcept       = 0;

    [[nodiscard]] virtual const Memory& mem() const noexcept = 0;
    [[nodiscard]] virtual Memory&       mem() noexcept       = 0;

    // Control word of the last instruction that committed in WB.
    [[nodiscard]] virtual const Control& last_control() const noexcept = 0;

    // Number of clock cycles elapsed since the last reset().
    [[nodiscard]] virtual std::size_t cycle_count() const noexcept = 0;

    // Snapshot of all five pipeline stages from the most recent step().
    // SingleCycleCpu only populates stages[2] (EX); PipelinedCpu fills all five.
    [[nodiscard]] virtual const PipelineState& pipeline_state() const noexcept = 0;
};

}  // namespace mips
