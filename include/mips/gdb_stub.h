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
//      connection drops. A halt or exception only ends the current 'c'/'s'
//      with a stop reply; GDB decides what happens next.
//
// A Ctrl-C (a bare 0x03 byte) stops a running 'c' within kInterruptPollSteps
// instructions and is answered with exactly one SIGINT stop reply. Incoming
// '}' escapes are decoded after the checksum is verified. Run-length encoding
// ('*') applies only to stub-to-GDB replies, which this stub never compresses.
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

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mips {

class GdbStub final {
public:
    // Construct a stub attached to `cpu`, listening on TCP `port`.
    // The stub does NOT take ownership of the processor.
    explicit GdbStub(IMipsProcessor& cpu, uint16_t port = 1234);
    ~GdbStub();

    GdbStub(const GdbStub&)            = delete;
    GdbStub& operator=(const GdbStub&) = delete;

    // Grants the unit test access to the private, non-throwing hex parsers so the
    // malformed-input contract can be checked without standing up a TCP session.
    friend struct GdbStubTestAccess;

    // Block until a GDB client connects, then run the RSP event loop.
    // Returns when GDB sends 'k'/'D', or the CPU halts, or the socket drops.
    void listen();

private:
    // Largest packet payload accepted, in raw (still-escaped) characters. It is
    // advertised to GDB as qSupported PacketSize, so a well-behaved client never
    // exceeds it; anything longer is NAKed instead of growing the buffer.
    static constexpr std::size_t kMaxPacketSize = 0x4000;
    // How many instructions 'c' runs between non-blocking checks for Ctrl-C.
    static constexpr std::size_t kInterruptPollSteps = 1024;

    // ── RSP packet I/O ────────────────────────────────────────────────────────
    enum class RecvStatus : std::uint8_t {
        Packet,     // `out` holds a verified, unescaped payload
        Interrupt,  // a bare 0x03 (Ctrl-C) arrived between packets
        Closed,     // the connection dropped or failed
    };
    enum class InputPoll : std::uint8_t {
        Idle,       // nothing that concerns a running 'c'
        Interrupt,  // Ctrl-C arrived; it has been consumed
        Closed,     // the peer closed the connection
    };

    void                     configure_client_socket();  // options + fresh rx buffer
    void                     serve();                    // RSP event loop on client_fd_
    bool                     send_packet(const std::string& data);
    bool                     send_raw(const std::string& s);
    [[nodiscard]] RecvStatus recv_packet(std::string& out);
    [[nodiscard]] bool       read_byte(char& c);  // blocking, buffered
    [[nodiscard]] InputPoll  poll_input();        // non-blocking
    void                     send_ok();
    void                     send_empty();
    void                     send_error(uint8_t code);
    void                     send_signal(int sig);

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
    static constexpr int   kNumRegs = 38;
    [[nodiscard]] uint32_t read_gdb_reg(int n) const;
    void                   write_gdb_reg(int n, uint32_t value);

    // ── Utilities ────────────────────────────────────────────────────────────
    static uint8_t     checksum(const std::string& data) noexcept;
    static std::string to_hex_le(uint32_t v);  // 4-byte little-endian hex
    static uint32_t    from_hex_le(const std::string& s, size_t off = 0);
    // True when the two received checksum characters are valid hex and equal
    // checksum(data).
    [[nodiscard]] static bool checksum_matches(const std::string& data, char hi, char lo);
    // Decode RSP binary escapes: '}' followed by c means c ^ 0x20. nullopt for a
    // payload that ends inside an escape.
    [[nodiscard]] static std::optional<std::string> unescape(const std::string& raw);
    // Non-throwing hex parsers: RSP payloads are attacker-controllable, so a
    // malformed field must yield nullopt rather than throw out of the packet
    // loop and abort the process.
    static std::optional<uint32_t> parse_hex(const std::string& s);
    static std::optional<uint8_t>  parse_hex_byte(const std::string& s, std::size_t off);
    static std::string             hex_byte(uint8_t b);

    // Signal number to send for a given StepResult / exception code.
    [[nodiscard]] int stop_signal() const;

    IMipsProcessor& cpu_;
    uint16_t        port_;
    int             server_fd_ = -1;
    int             client_fd_ = -1;

    // Receive buffer: one recv() fills it, read_byte() drains it. Bytes at
    // [rx_begin_, rx_end_) are received but not yet consumed.
    std::array<char, 4096> rx_buf_{};
    std::size_t            rx_begin_ = 0;
    std::size_t            rx_end_   = 0;

    std::vector<Breakpoint> breakpoints_;
    bool                    running_     = false;  // true while inside handle_continue
    bool                    detached_    = false;  // set by 'k' / 'D'; ends serve()
    StepResult              last_result_ = StepResult::Ok;
};

}  // namespace mips
