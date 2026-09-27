#pragma once

// ─── pipeline_regs.h ─────────────────────────────────────────────────────────
// Plain-data structs that model the four inter-stage registers of a classic
// 5-stage MIPS pipeline (H&H §8.4, Figure 8.22).  All fields are zero-
// initialised so an empty (bubble) register is simply a value-initialised
// struct — no sentinel values, no separate "nop" encoding.
//
// Each struct is named for the pair of stages it sits between:
//   IfId  — between Fetch and Decode
//   IdEx  — between Decode and Execute
//   ExMem — between Execute and Memory
//   MemWb — between Memory and Writeback
//
// The `rs` / `rt` fields duplicated in IdEx are the *register indices*
// (not values) needed by the hazard-detection unit and forwarding unit in
// the Execute stage — keeping them here avoids re-parsing the raw instruction
// word mid-pipeline.
//
// Traps found before EX — a failed fetch (AdEL) or a word that does not
// decode (RI) — are recorded in the register and ride down to EX, where they
// are raised. Every older instruction is then in MEM or WB, and a trap in MEM
// in the same cycle wins, so exceptions are raised in program order. A flush
// on the way down discards a wrong-path trap like any other wrong-path work.

#include "mips/alu.h"
#include "mips/cp0.h"
#include "mips/decoder.h"
#include "mips/processor.h"

#include <optional>

namespace mips {

// ─── IF / ID ─────────────────────────────────────────────────────────────────
struct IfId {
    uint32_t pc          = 0;      // address of the fetched instruction
    uint32_t pc4         = 0;      // pc + 4 (branch target base; passed forward)
    uint32_t instr       = 0;      // raw 32-bit machine word
    bool     valid       = false;  // false → bubble
    bool     is_halt     = false;  // J/JAL self-target detected at IF
    bool     fetch_fault = false;  // fetching `pc` failed: AdEL, raised in EX; `instr` is 0
};

// ─── ID / EX ─────────────────────────────────────────────────────────────────
struct IdEx {
    uint32_t                     pc  = 0;
    uint32_t                     pc4 = 0;
    Control                      ctrl{};
    DecodedInstr                 decoded{};    // full decoded instruction (for Alu::control in EX)
    uint32_t                     rs_val  = 0;  // value read from register file (may be stale;
    uint32_t                     rt_val  = 0;  //   forwarding in EX overrides these if needed)
    uint8_t                      rs      = 0;  // register indices — needed by:
    uint8_t                      rt      = 0;  //   hazard unit (next cycle) and forwarding unit
    bool                         valid   = false;
    bool                         is_halt = false;
    std::optional<ExceptionCode> early_trap;  // AdEL (fetch) or RI (decode), raised in EX
};

// ─── EX / MEM ────────────────────────────────────────────────────────────────
struct ExMem {
    uint32_t  pc  = 0;
    uint32_t  raw = 0;  // machine word, carried so MEM/WB snapshots can show a mnemonic
    Control   ctrl{};
    AluResult alu{};
    uint32_t  rt_val    = 0;                // forwarded rt, used by SW as the data to write
    uint8_t   write_reg = 0;                // destination register (rd, rt, or $ra for JAL)
    Opcode    opcode    = Opcode::SPECIAL;  // needed by MEM to pick load width
    bool      valid     = false;
    bool      is_halt   = false;
};

// ─── MEM / WB ────────────────────────────────────────────────────────────────
struct MemWb {
    uint32_t pc  = 0;
    uint32_t raw = 0;
    Control  ctrl{};
    uint32_t alu_val   = 0;
    uint32_t mem_val   = 0;  // populated only for loads
    uint8_t  write_reg = 0;
    bool     valid     = false;
    bool     is_halt   = false;
};

}  // namespace mips
