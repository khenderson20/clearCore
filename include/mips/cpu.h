#pragma once

#include "mips/alu.h"
#include "mips/decoder.h"
#include "mips/memory.h"
#include "mips/registers.h"

#include <cstdint>
#include <vector>

namespace mips {

// ─── Control word ─────────────────────────────────────────────────────────────
// The single-cycle control signals (H&H Figure 7.11), derived purely from the
// opcode (and funct for R-type). These drive the muxes and write-enables of the
// datapath. Exposed publicly so the Stage 2 TUI can show them per cycle.
//
// JR/JALR/JAL are register/link jumps that don't fit the plain "ALU → register"
// path; control() captures their reg_write/jump intent and Cpu::step() handles
// the address redirect explicitly.
struct Control {
    bool reg_write  = false;   // write a result back to a register
    bool mem_read   = false;   // load:  read data memory
    bool mem_write  = false;   // store: write data memory
    bool mem_to_reg = false;   // writeback source: 1 = memory, 0 = ALU
    bool alu_src    = false;   // ALU operand b: 1 = immediate, 0 = register rt
    bool reg_dst    = false;   // destination field: 1 = rd, 0 = rt
    bool branch     = false;   // conditional branch (BEQ/BNE)
    bool jump       = false;   // unconditional jump (J/JAL)

    // How the immediate becomes ALU operand b when alu_src is set.
    enum class Ext : uint8_t { Sign, Zero } ext = Ext::Sign;
};

// ─── Step outcome ─────────────────────────────────────────────────────────────
enum class StepResult : uint8_t {
    Ok,     // an instruction executed; PC advanced normally
    Fault,  // undecodable instruction or bad/misaligned memory access — stopped
    Halt,   // a self-targeting jump/branch was hit (the spin-in-place idiom)
};

// ─── Single-cycle MIPS CPU ────────────────────────────────────────────────────
// One step() runs the whole datapath in a single tick:
//   fetch → decode → control → execute(ALU) → memory → writeback → PC update
//
// Memory holds both instructions and data (von Neumann, for simplicity — the
// datapath visualizer doesn't need split I/D memories). Exceptions are not
// modelled: signed-overflow traps and address-error traps surface as a Fault
// rather than a handler vector.
class Cpu {
public:
    explicit Cpu(std::size_t mem_bytes = 1u << 16);   // 64 KiB default

    // Load a flat array of instruction words at `addr` and point the PC there.
    // Returns false if the image does not fit in memory.
    bool load_program(const std::vector<uint32_t>& words, uint32_t addr = 0);

    // Execute exactly one instruction. See StepResult.
    StepResult step();

    // Step until Fault/Halt or `max_steps` instructions have run. Returns the
    // result that stopped it (Ok means the step budget was exhausted, which is
    // the runaway-program guard).
    StepResult run(std::size_t max_steps = 100'000);

    // Reset registers, PC, and the control snapshot. Memory is left intact so a
    // loaded program can be re-run; pass clear_memory=true to wipe it too.
    void reset(bool clear_memory = false);

    // ── Accessors for tests and the TUI ──────────────────────────────────────
    [[nodiscard]] uint32_t pc() const noexcept { return pc_; }
    void set_pc(uint32_t pc) noexcept { pc_ = pc; }

    [[nodiscard]] RegisterFile&       regs()       noexcept { return regs_; }
    [[nodiscard]] const RegisterFile& regs() const noexcept { return regs_; }
    [[nodiscard]] Memory&             mem()        noexcept { return mem_; }
    [[nodiscard]] const Memory&       mem()  const noexcept { return mem_; }

    // Control word of the most recently executed instruction (for TUI panels).
    [[nodiscard]] const Control& last_control() const noexcept { return ctrl_; }

    // Derive the control word for a decoded instruction. Pure; no side effects.
    [[nodiscard]] static Control control(const DecodedInstr& instr);

private:
    bool exec_rtype(const DecodedInstr& d, uint32_t pc_plus_4, uint32_t& next_pc);
    bool exec_itype(const DecodedInstr& d, uint32_t pc_plus_4, uint32_t& next_pc);

    RegisterFile regs_;
    Memory       mem_;
    uint32_t     pc_ = 0;
    Control      ctrl_{};

    static constexpr uint8_t kRa = 31;   // $ra — JAL/JALR link register
};

} // namespace mips