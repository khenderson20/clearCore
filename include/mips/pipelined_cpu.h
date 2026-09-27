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
//
// Structure: one private function per stage. Each reads only the pipeline
// registers as they were at the start of the cycle and returns an outcome;
// step() runs them oldest first and resolves which trap or redirect wins.

#include "mips/cp0.h"
#include "mips/pipeline_regs.h"
#include "mips/pipeline_units.h"
#include "mips/processor.h"

#include <cstdint>
#include <optional>
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
    // ── Stage outcomes ───────────────────────────────────────────────────────
    // A trap a stage detected. step() raises at most one per cycle: the oldest.
    struct Trap {
        ExceptionCode code     = ExceptionCode::RI;
        uint32_t      pc       = 0;
        uint32_t      bad_addr = 0;
    };

    // MEM performs its own load or store: no stage older than MEM can trap.
    struct MemOutcome {
        MemWb               next{};
        std::optional<Trap> trap;  // AdEL / AdES
    };

    // EX changes no state itself. Its CP0 access and control transfer are applied
    // by step() only when no older stage trapped, so a squashed instruction leaves
    // no trace.
    struct Mtc0Write {
        uint8_t  reg   = 0;
        uint32_t value = 0;
    };
    struct ExOutcome {
        ExMem                    next{};
        std::optional<Trap>      trap;      // Sys, Bp, Ov, RI
        std::optional<uint32_t>  redirect;  // taken branch, JR/JALR target, ERET's EPC
        bool                     eret = false;
        std::optional<Mtc0Write> mtc0;
        ForwardingPaths          forwarded{};
    };

    struct IdOutcome {
        IdEx                    next{};
        std::optional<Trap>     trap;  // RI: the word does not decode
        std::optional<uint32_t> jump;  // J/JAL target, resolved in ID (1-stage flush)
    };

    struct IfOutcome {
        IfId                next{};
        std::optional<Trap> trap;  // AdEL: the fetch address is out of range or misaligned
    };

    [[nodiscard]] bool       write_back(const MemWb& in);  // true when a halt retired
    [[nodiscard]] MemOutcome memory_access(const ExMem& in);
    [[nodiscard]] ExOutcome execute(const IdEx& in, const ExMem& ex_mem, const MemWb& mem_wb) const;
    [[nodiscard]] IdOutcome decode(const IfId& in) const;
    [[nodiscard]] IfOutcome fetch() const;

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
