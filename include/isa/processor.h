#pragma once

// ─── isa/processor.h ─────────────────────────────────────────────────────────
// Abstract, ISA-agnostic processor interface, modelled on the pluggable-backend
// pattern used by Ripes (Petersen, 2021) and DrMIPS (Nova et al., 2013).
//
// Everything here is common to every ISA clearCore models (MIPS today, RISC-V
// next): a program image of 32-bit words, single-cycle stepping, a 32×32
// register file, flat memory, and a five-stage pipeline snapshot for the
// visualisers. ISA-specific state (MIPS coprocessor 0 / HI / LO, RISC-V CSRs)
// lives in per-backend sub-interfaces, not here.
//
//   1. Observable pipeline state is part of the interface (DrMIPS exposes the
//      datapath graphically; WebRISC-V exposes cycle-by-cycle stage contents).
//   2. step() advances exactly one clock cycle, regardless of implementation.
//   3. run() is a default loop over step() so callers never need to change it.
//   4. Forwarding and stall flags are first-class so the TUI can draw bypass
//      wires and bubble indicators (Arches, Haydel et al. 2025).

#include "isa/memory.h"
#include "isa/registers.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace isa {

// ─── StepResult ───────────────────────────────────────────────────────────────
enum class StepResult : uint8_t {
    Ok,         // instruction executed; PC advanced normally
    Exception,  // an ISA trap was raised; the backend updated its trap state and
                // redirected the PC to its exception/trap vector
    Fault,      // unrecoverable internal error (should not occur in correct programs)
    Halt,       // self-targeting jump retired (spin-in-place idiom)
};

// ─── Pipeline visualisation state ─────────────────────────────────────────────
// Filled by every implementation; single-cycle backends leave all but EX empty.
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

    // Forwarding indicators (bypass-wire visualisation)
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
// The ISA-agnostic contract. The TUI, Qt bridge, and generic tests talk only to
// this. Backends that expose extra architectural state (MIPS CP0/HI/LO) derive a
// sub-interface — see mips::IMipsProcessor.
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

    // Reset registers, PC, and pipeline snapshot.
    // Pass clear_memory=true to also wipe the address space.
    virtual void reset(bool clear_memory = false) = 0;

    // ── Accessors ──────────────────────────────────────────────────────────────

    [[nodiscard]] virtual uint32_t pc() const noexcept            = 0;
    virtual void                   set_pc(uint32_t addr) noexcept = 0;

    [[nodiscard]] virtual const RegisterFile& regs() const noexcept = 0;
    [[nodiscard]] virtual RegisterFile&       regs() noexcept       = 0;

    [[nodiscard]] virtual const Memory& mem() const noexcept = 0;
    [[nodiscard]] virtual Memory&       mem() noexcept       = 0;

    // Number of clock cycles elapsed since the last reset().
    [[nodiscard]] virtual std::size_t cycle_count() const noexcept = 0;

    // Snapshot of all five pipeline stages from the most recent step().
    // Single-cycle backends only populate stages[2] (EX); pipelined ones fill all five.
    [[nodiscard]] virtual const PipelineState& pipeline_state() const noexcept = 0;
};

}  // namespace isa
