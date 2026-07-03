#pragma once

#include <cstdint>
#include <string_view>

namespace mips {

// MIPS exception codes (Cause register ExcCode field [6:2]).
// MIPS32r2 §6.1.13 Table 6-1 — subset relevant to a 5-stage integer pipeline.
enum class ExceptionCode : uint8_t {
    Int  = 0,   // Hardware interrupt (not internally generated)
    AdEL = 4,   // Address error on instruction fetch or data load
    AdES = 5,   // Address error on data store
    Sys  = 8,   // SYSCALL instruction
    Bp   = 9,   // BREAK instruction
    RI   = 10,  // Reserved (unrecognised) instruction
    Ov   = 12,  // Arithmetic overflow (ADD, ADDI, SUB)
};

[[nodiscard]] std::string_view exception_name(ExceptionCode code) noexcept;

// General exception vector (Status.BEV=0, MIPS32r2 §6.3).
// Programs that install a handler must map code here.  The GDB stub intercepts
// exceptions before they vector, so this address is never actually fetched when
// running under GDB.
constexpr uint32_t kExceptionVector = 0x8000'0180u;

// ─── Cp0 ──────────────────────────────────────────────────────────────────────
// Minimal Coprocessor 0 for a 5-stage educational pipeline.
// Only the four registers needed by the exception model and the GDB stub
// are implemented; all others read as 0 and ignore writes.
//
// Status register (CP0[12]):
//   bit 0  IE  — interrupt enable (modelled but not acted upon)
//   bit 1  EXL — exception level; 1 while inside an exception handler
//
// Cause register (CP0[13]):
//   bits 6:2  ExcCode — most recent exception code
//
// EPC register (CP0[14]):
//   PC of the instruction that caused the exception.
//   ERET restores the CPU's PC from this register.
//
// BadVAddr register (CP0[8]):
//   Virtual address that triggered AdEL or AdES.
class Cp0 {
public:
    static constexpr uint8_t  kRegBadVAddr = 8;
    static constexpr uint8_t  kRegStatus   = 12;
    static constexpr uint8_t  kRegCause    = 13;
    static constexpr uint8_t  kRegEpc      = 14;
    static constexpr uint32_t kStatusEXL   = 1u << 1;  // Exception Level bit

    // Raise an exception.
    //   • Writes faulting_pc to EPC.
    //   • Encodes code into Cause.ExcCode[6:2].
    //   • Sets Status.EXL = 1.
    //   • Stores bad_addr in BadVAddr (for AdEL / AdES only).
    // Returns kExceptionVector — the address the CPU's PC should be set to.
    uint32_t raise(ExceptionCode code, uint32_t faulting_pc, uint32_t bad_addr = 0) noexcept;

    // Execute ERET: clears Status.EXL and returns EPC.
    // The CPU should set its PC to the returned value.
    uint32_t eret() noexcept;

    // MFC0 / MTC0 register access.  Reads of unknown registers return 0;
    // writes to unknown registers are silently ignored.
    [[nodiscard]] uint32_t read(uint8_t reg) const noexcept;
    void                   write(uint8_t reg, uint32_t value) noexcept;

    [[nodiscard]] bool     in_exception() const noexcept { return (status_ & kStatusEXL) != 0; }
    [[nodiscard]] uint32_t status() const noexcept { return status_; }
    [[nodiscard]] uint32_t cause() const noexcept { return cause_; }
    [[nodiscard]] uint32_t epc() const noexcept { return epc_; }
    [[nodiscard]] uint32_t bad_vaddr() const noexcept { return bad_vaddr_; }

    // Most recent exception code (meaningful after raise() has been called).
    [[nodiscard]] ExceptionCode last_exception() const noexcept { return last_exc_; }

    void reset() noexcept;

private:
    uint32_t      status_    = 0;
    uint32_t      cause_     = 0;
    uint32_t      epc_       = 0;
    uint32_t      bad_vaddr_ = 0;
    ExceptionCode last_exc_  = ExceptionCode::RI;
};

}  // namespace mips
