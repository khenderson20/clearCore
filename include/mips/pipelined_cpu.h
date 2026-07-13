#pragma once

// ─── pipelined_cpu.h ─────────────────────────────────────────────────────────
// Classic 5-stage MIPS pipeline: IF → ID → EX → MEM → WB.
//
// Stage ordering within each step():
//   WB first (writes to register file) → MEM → EX → ID (reads register file
//   after WB wrote) → IF. Processing WB before ID means that when an
//   instruction in WB writes $t0 and the instruction in ID reads $t0 in the
//   same cycle, ID sees the updated value — this is the "register-file internal
//   forwarding" that H&H describes in §8.4 and that WebRISC-V models.
//
// Hazard handling (H&H §8.5 / Arches stall-type logging, Haydel 2025):
//   • Load-use: 1-cycle stall — bubble inserted in ID/EX, IF/ID held, PC held.
//   • Data forwarding: EX/MEM → EX (priority) and MEM/WB → EX (secondary).
//   • Control (J/JAL): resolved in ID, 1-cycle flush (IF result discarded).
//   • Control (BEQ/BNE, JR/JALR): resolved in EX, 2-cycle flush (IF+ID).
//
// Halt convention: a self-targeting J/JAL is marked is_halt at IF. The flag
// propagates through all four pipeline registers unchanged. When it exits WB,
// step() returns StepResult::Halt. All preceding instructions have already
// retired by then, so register/memory state is final and consistent.

#include "mips/pipeline_regs.h"
#include "mips/processor.h"

#include <vector>

namespace mips {

class PipelinedCpu final : public IMipsProcessor {
public:
    explicit PipelinedCpu(std::size_t mem_bytes = 1u << 16);

    bool       load_program(const std::vector<uint32_t>& words, uint32_t addr = 0) override;
    StepResult step() override;
    void       reset(bool clear_memory = false) override;

    [[nodiscard]] uint32_t             pc() const noexcept override { return pc_; }
    void                               set_pc(uint32_t p) noexcept override { pc_ = p; }
    [[nodiscard]] const RegisterFile&  regs() const noexcept override { return regs_; }
    [[nodiscard]] RegisterFile&        regs() noexcept override { return regs_; }
    [[nodiscard]] const Memory&        mem() const noexcept override { return mem_; }
    [[nodiscard]] Memory&              mem() noexcept override { return mem_; }
    [[nodiscard]] const Control&       last_control() const noexcept override { return ctrl_; }
    [[nodiscard]] std::size_t          cycle_count() const noexcept override { return cycle_; }
    [[nodiscard]] const PipelineState& pipeline_state() const noexcept override { return ps_; }
    [[nodiscard]] const Cp0&           cp0() const noexcept override { return cp0_; }
    [[nodiscard]] Cp0&                 cp0() noexcept override { return cp0_; }
    [[nodiscard]] uint32_t             hi() const noexcept override { return hi_; }
    [[nodiscard]] uint32_t             lo() const noexcept override { return lo_; }
    void                               set_hi(uint32_t v) noexcept override { hi_ = v; }
    void                               set_lo(uint32_t v) noexcept override { lo_ = v; }

private:
    RegisterFile  regs_;
    Memory        mem_;
    Cp0           cp0_{};
    uint32_t      pc_ = 0;
    uint32_t      hi_ = 0;
    uint32_t      lo_ = 0;
    Control       ctrl_{};
    std::size_t   cycle_ = 0;
    PipelineState ps_{};

    // The four inter-stage registers
    IfId  if_id_{};
    IdEx  id_ex_{};
    ExMem ex_mem_{};
    MemWb mem_wb_{};
};

}  // namespace mips
