#pragma once

// ─── single_cycle_cpu.h ───────────────────────────────────────────────────────
// Single-cycle (combinational) datapath.  Every step() runs the full
// fetch → decode → control → execute → memory → writeback chain before
// returning.  This is the straightforward H&H Chapter 7 design.
//
// It implements IProcessor so the TUI and tests can treat it identically to
// PipelinedCpu.  The PipelineState it exposes only populates the EX snapshot
// (the only stage that "exists" in a single-cycle design), matching the DrMIPS
// "unicycle" display mode.

#include "mips/processor.h"

#include <vector>

namespace mips {

class SingleCycleCpu final : public IProcessor {
public:
    explicit SingleCycleCpu(std::size_t mem_bytes = 1u << 16);

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
    // Returns StepResult::Exception after calling cp0_.raise() and updating pc_.
    StepResult raise(ExceptionCode code, uint32_t faulting_pc, uint32_t bad_addr = 0) noexcept;

    bool exec_rtype(const DecodedInstr& d, uint32_t pc4, uint32_t& next_pc, StepResult& exc);
    bool exec_itype(const DecodedInstr& d, uint32_t pc4, uint32_t& next_pc, StepResult& exc);

    RegisterFile  regs_;
    Memory        mem_;
    Cp0           cp0_{};
    uint32_t      pc_ = 0;
    uint32_t      hi_ = 0;
    uint32_t      lo_ = 0;
    Control       ctrl_{};
    std::size_t   cycle_ = 0;
    PipelineState ps_{};

    static constexpr uint8_t kRa = 31;
};

}  // namespace mips
