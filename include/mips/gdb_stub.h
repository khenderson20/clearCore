#pragma once

// ─── gdb_stub.h ──────────────────────────────────────────────────────────────
// GDB Remote Serial Protocol (RSP) server for clearCore's MIPS emulator.
//
// How it works:
//   1. Call listen() — this blocks until a GDB client connects on `port`.
//   2. The stub enters an RSP event loop: GDB sends commands (step, continue,
//      read registers, read/write memory, set/remove breakpoints) and the stub
//      translates them to IProcessor calls.
//   3. The loop exits when GDB sends 'k' (kill) or 'D' (detach), or when the
//      CPU halts (self-targeting jump) or the connection drops.
//
// Supported RSP commands:
//   ?           — stop reason (always SIGTRAP initially)
//   g / G       — read / write all 38 MIPS registers
//   p n / P n=v — read / write single register
//   m addr,len  — read memory
//   M addr,len:data — write memory
//   c [addr]    — continue execution
//   s [addr]    — step one instruction
//   Z0,a,k / z0,a,k — insert / remove software breakpoint
//   k           — kill (stop loop)
//   D           — detach (stop loop)
//   qSupported  — feature negotiation
//   qAttached   — query attach mode
//   qC          — current thread
//   H / T       — thread selection / alive (ignored)
//   vCont?      — not supported (GDB falls back to c/s)
//
// MIPS register layout (GDB MIPS32 ABI, 38 registers × 4 bytes):
//   0–31   general-purpose r0–r31
//   32     CP0 Status
//   33     LO
//   34     HI
//   35     CP0 BadVAddr
//   36     CP0 Cause
//   37     PC

#include "mips/processor.h"

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace mips {

class GdbStub {
public:
    // Construct a stub attached to `cpu`, listening on TCP `port`.
    // The stub does NOT take ownership of the processor.
    explicit GdbStub(IMipsProcessor& cpu, uint16_t port = 1234);
    ~GdbStub();

    GdbStub(const GdbStub&)            = delete;
    GdbStub& operator=(const GdbStub&) = delete;

    // Block until a GDB client connects, then run the RSP event loop.
    // Returns when GDB sends 'k'/'D', or the CPU halts, or the socket drops.
    void listen();

private:
    // ── RSP packet I/O ────────────────────────────────────────────────────────
    bool send_packet(const std::string& data);
    bool send_raw(const std::string& s);
    bool recv_packet(std::string& out);
    void send_ok();
    void send_empty();
    void send_error(uint8_t code);
    void send_signal(int sig);

    // ── RSP command handlers ──────────────────────────────────────────────────
    std::string handle_read_regs();
    bool        handle_write_regs(const std::string& hex);
    std::string handle_read_reg(const std::string& args);
    bool        handle_write_reg(const std::string& args);
    std::string handle_read_mem(const std::string& args);
    bool        handle_write_mem(const std::string& args);
    void        handle_continue(const std::string& args);
    void        handle_step(const std::string& args);
    void        handle_breakpoint_set(const std::string& args);
    void        handle_breakpoint_clear(const std::string& args);
    void        dispatch(const std::string& pkt);

    // ── Breakpoint helpers ────────────────────────────────────────────────────
    // Software breakpoints: GDB replaces the target instruction with BREAK
    // (0x0000000d) and restores it on removal.  We track the original word
    // so we can restore it on z0.
    struct Breakpoint {
        uint32_t addr;
        uint32_t saved_word;  // original instruction replaced by BREAK
    };
    bool insert_breakpoint(uint32_t addr);
    bool remove_breakpoint(uint32_t addr);

    // ── Register access (MIPS GDB layout) ────────────────────────────────────
    static constexpr int kNumRegs = 38;
    uint32_t             read_gdb_reg(int n) const;
    void                 write_gdb_reg(int n, uint32_t value);

    // ── Utilities ────────────────────────────────────────────────────────────
    static uint8_t     checksum(const std::string& data) noexcept;
    static std::string to_hex_le(uint32_t v);  // 4-byte little-endian hex
    static uint32_t    from_hex_le(const std::string& s, size_t off = 0);
    static uint32_t    parse_hex(const std::string& s);
    static std::string hex_byte(uint8_t b);

    // Signal number to send for a given StepResult / exception code.
    int stop_signal() const;

    IMipsProcessor& cpu_;
    uint16_t        port_;
    int             server_fd_ = -1;
    int             client_fd_ = -1;

    std::vector<Breakpoint> breakpoints_;
    bool                    running_     = false;  // true while inside handle_continue
    StepResult              last_result_ = StepResult::Ok;
};

}  // namespace mips
