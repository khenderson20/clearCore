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

#include "mips/decoder.h"
#include "mips/alu.h"
#include "mips/processor.h"

namespace mips {

// ─── IF / ID ─────────────────────────────────────────────────────────────────
struct IfId {
    uint32_t pc      = 0;      // address of the fetched instruction
    uint32_t pc4     = 0;      // pc + 4 (branch target base; passed forward)
    uint32_t instr   = 0;      // raw 32-bit machine word
    bool     valid   = false;  // false → bubble
    bool     is_halt = false;  // J/JAL self-target detected at IF
};

// ─── ID / EX ─────────────────────────────────────────────────────────────────
struct IdEx {
    uint32_t     pc       = 0;
    uint32_t     pc4      = 0;
    Control      ctrl{};
    DecodedInstr decoded{};    // full decoded instruction (for Alu::control in EX)
    uint32_t     rs_val   = 0; // value read from register file (may be stale;
    uint32_t     rt_val   = 0; //   forwarding in EX overrides these if needed)
    uint8_t      rs       = 0; // register indices — needed by:
    uint8_t      rt       = 0; //   hazard unit (next cycle) and forwarding unit
    bool         valid    = false;
    bool         is_halt  = false;
};

// ─── EX / MEM ────────────────────────────────────────────────────────────────
struct ExMem {
    uint32_t  pc         = 0;
    Control   ctrl{};
    AluResult alu{};
    uint32_t  rt_val     = 0;  // forwarded rt, used by SW as the data to write
    uint8_t   write_reg  = 0;  // destination register (rd, rt, or $ra for JAL)
    Opcode    opcode     = Opcode::SPECIAL; // needed by MEM to pick load width
    bool      valid      = false;
    bool      is_halt    = false;
};

// ─── MEM / WB ────────────────────────────────────────────────────────────────
struct MemWb {
    uint32_t pc        = 0;
    Control  ctrl{};
    uint32_t alu_val   = 0;
    uint32_t mem_val   = 0;    // populated only for loads
    uint8_t  write_reg = 0;
    bool     valid     = false;
    bool     is_halt   = false;
};

} // namespace mips
