#include "mips/gdb_stub.h"
#include "mips/cp0.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <exception>
#include <gsl/gsl>
#include <string>
#include <system_error>
#include <utility>

// POSIX socket headers — supported on Linux and macOS.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

namespace mips {

// MIPS BREAK instruction word (opcode=SPECIAL, funct=BREAK=0x0D, code field 0).
static constexpr uint32_t kBreakWord = 0x0000'000Du;

// POSIX signal numbers mirrored here to avoid including <signal.h>.
static constexpr int kSIGINT  = 2;
static constexpr int kSIGTRAP = 5;
static constexpr int kSIGSEGV = 11;
static constexpr int kSIGILL  = 4;
static constexpr int kSIGFPE  = 8;
static constexpr int kSIGSYS  = 12;

// A peer that disconnects mid-reply must not raise SIGPIPE: its default action
// kills the whole emulator. Linux has a per-call flag for this; macOS has only
// the per-socket SO_NOSIGPIPE option, which configure_client_socket() sets.
#if defined(MSG_NOSIGNAL)
static constexpr int kSendFlags = MSG_NOSIGNAL;
#else
static constexpr int kSendFlags = 0;
#endif

// ─── Constructor / destructor ─────────────────────────────────────────────────

GdbStub::GdbStub(IMipsProcessor& cpu, uint16_t port) : cpu_(cpu), port_(port) {}

GdbStub::~GdbStub() {
    if (client_fd_ >= 0) ::close(client_fd_);
    if (server_fd_ >= 0) ::close(server_fd_);
}

// ─── listen ───────────────────────────────────────────────────────────────────

void GdbStub::listen() {
    server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) return;

    // Close and invalidate both sockets on *every* exit path — the bind-failure
    // and accept-failure early returns included. gsl::finally keeps that
    // guarantee in one place; the previous accept-failure return leaked the
    // listening socket until the GdbStub was destroyed.
    auto sockets = gsl::finally([this] {
        if (client_fd_ >= 0) {
            ::close(client_fd_);
            client_fd_ = -1;
        }
        if (server_fd_ >= 0) {
            ::close(server_fd_);
            server_fd_ = -1;
        }
    });

    const int yes = 1;
    ::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = htons(port_);

    if (::bind(server_fd_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) return;
    ::listen(server_fd_, 1);

    sockaddr_in peer{};
    socklen_t   peer_len = sizeof(peer);
    client_fd_           = ::accept(server_fd_, reinterpret_cast<sockaddr*>(&peer), &peer_len);
    if (client_fd_ < 0) return;

    configure_client_socket();
    serve();
}

void GdbStub::configure_client_socket() {
    const int yes = 1;
    // Disable Nagle — RSP is request-response, latency matters more than throughput.
    // Fails harmlessly on a non-TCP socket (the unit tests use a socketpair).
    ::setsockopt(client_fd_, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
#if defined(SO_NOSIGPIPE)
    ::setsockopt(client_fd_, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif
    rx_begin_ = 0;
    rx_end_   = 0;
}

// ─── RSP event loop ──────────────────────────────────────────────────────────

void GdbStub::serve() {
    last_result_ = StepResult::Ok;
    detached_    = false;
    std::string pkt;
    while (!detached_) {
        const RecvStatus status = recv_packet(pkt);
        if (status == RecvStatus::Closed) return;
        if (status == RecvStatus::Interrupt) {
            // Ctrl-C while the target is already stopped: GDB still waits for the
            // stop reply its interrupt asked for. Send exactly one, and never
            // dispatch the interrupt as if it were an (empty) packet.
            send_signal(kSIGINT);
            continue;
        }
        // Defense in depth: the individual handlers are written not to throw on
        // malformed input, but a single bad packet must never abort the whole
        // emulator, so any stray exception degrades to an RSP error reply.
        try {
            dispatch(pkt);
        } catch (const std::exception&) {
            send_error(0);
        }
    }
}

// ─── Packet I/O ──────────────────────────────────────────────────────────────

uint8_t GdbStub::checksum(const std::string& data) noexcept {
    uint8_t sum = 0;
    for (const unsigned char c : data)
        sum = static_cast<uint8_t>(sum + c);
    return sum;
}

std::string GdbStub::hex_byte(uint8_t b) {
    char buf[3];
    std::snprintf(buf, sizeof(buf), "%02x", b);
    return {buf};
}

bool GdbStub::send_raw(const std::string& s) {
    const char* p   = s.data();
    auto        rem = static_cast<ssize_t>(s.size());
    while (rem > 0) {
        const ssize_t n = ::send(client_fd_, p, static_cast<size_t>(rem), kSendFlags);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        p += n;
        rem -= n;
    }
    return true;
}

bool GdbStub::send_packet(const std::string& data) {
    const std::string pkt = "$" + data + "#" + hex_byte(checksum(data));
    return send_raw(pkt);
}

void GdbStub::send_ok() {
    send_packet("OK");
}
void GdbStub::send_empty() {
    send_packet("");
}
void GdbStub::send_error(uint8_t code) {
    send_packet("E" + hex_byte(code));
}

void GdbStub::send_signal(int sig) {
    char buf[4];
    std::snprintf(buf, sizeof(buf), "S%02x", static_cast<unsigned>(sig));
    send_packet(buf);
}

bool GdbStub::checksum_matches(const std::string& data, char hi, char lo) {
    const std::string digits{hi, lo};
    const auto        expected = parse_hex_byte(digits, 0);
    return expected && *expected == checksum(data);
}

std::optional<std::string> GdbStub::unescape(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] != '}') {
            out += raw[i];
            continue;
        }
        if (++i == raw.size()) return std::nullopt;  // '}' with nothing to escape
        out += static_cast<char>(raw[i] ^ 0x20);
    }
    return out;
}

bool GdbStub::read_byte(char& c) {
    while (rx_begin_ == rx_end_) {
        const ssize_t n = ::recv(client_fd_, rx_buf_.data(), rx_buf_.size(), 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        rx_begin_ = 0;
        rx_end_   = static_cast<std::size_t>(n);
    }
    c = rx_buf_[rx_begin_++];
    return true;
}

GdbStub::InputPoll GdbStub::poll_input() {
    // Keep the unread bytes at the front so the non-blocking read below has room.
    if (rx_begin_ > 0) {
        std::copy(rx_buf_.begin() + static_cast<std::ptrdiff_t>(rx_begin_),
                  rx_buf_.begin() + static_cast<std::ptrdiff_t>(rx_end_), rx_buf_.begin());
        rx_end_ -= rx_begin_;
        rx_begin_ = 0;
    }
    // A full buffer holds no 0x03 (the previous poll scanned it), and in all-stop
    // mode GDB sends nothing but the interrupt while the target runs: drop it.
    if (rx_end_ == rx_buf_.size()) rx_end_ = 0;

    const ssize_t n =
        ::recv(client_fd_, rx_buf_.data() + rx_end_, rx_buf_.size() - rx_end_, MSG_DONTWAIT);
    const bool closed =
        n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR);
    if (n > 0) rx_end_ += static_cast<std::size_t>(n);

    // Scan before reporting a close: a peer that sends Ctrl-C and then hangs up
    // still gets the interrupt honoured.
    for (std::size_t i = rx_begin_; i < rx_end_; ++i) {
        if (rx_buf_[i] == '\x03') {
            rx_begin_ = i + 1;  // consume everything up to and including the interrupt
            return InputPoll::Interrupt;
        }
    }
    return closed ? InputPoll::Closed : InputPoll::Idle;
}

GdbStub::RecvStatus GdbStub::recv_packet(std::string& out) {
    // A packet that fails its checksum, exceeds kMaxPacketSize or ends inside an
    // escape is NAKed ('-'); the peer then retransmits, so loop until one arrives
    // intact or the connection drops.
    while (true) {
        out.clear();
        char c = 0;
        // Skip acks and noise until '$'. A bare 0x03 between packets is Ctrl-C.
        do {
            if (!read_byte(c)) return RecvStatus::Closed;
            if (c == '\x03') return RecvStatus::Interrupt;
        } while (c != '$');

        // Read until '#'. Past the size limit, keep reading to find the end of
        // the packet but stop storing it, so memory stays bounded.
        bool too_long = false;
        while (true) {
            if (!read_byte(c)) return RecvStatus::Closed;
            if (c == '#') break;
            if (out.size() < kMaxPacketSize)
                out += c;
            else
                too_long = true;
        }
        char hi = 0;
        char lo = 0;
        if (!read_byte(hi) || !read_byte(lo)) return RecvStatus::Closed;
        if (!too_long && checksum_matches(out, hi, lo)) {
            // The checksum covers the escaped bytes, so decode only after it passes.
            if (auto decoded = unescape(out)) {
                out = std::move(*decoded);
                send_raw("+");  // ACK
                return RecvStatus::Packet;
            }
        }
        send_raw("-");  // NAK
    }
}

// ─── Register access ─────────────────────────────────────────────────────────
// MIPS GDB register numbering (38 registers):
//   0–31  r0–r31 (general purpose)
//   32    CP0 Status
//   33    LO
//   34    HI
//   35    CP0 BadVAddr
//   36    CP0 Cause
//   37    PC

uint32_t GdbStub::read_gdb_reg(int n) const {
    if (n >= 0 && n < 32) return cpu_.regs().read(static_cast<uint8_t>(n));
    if (n == 32) return cpu_.cp0().status();
    if (n == 33) return cpu_.lo();
    if (n == 34) return cpu_.hi();
    if (n == 35) return cpu_.cp0().bad_vaddr();
    if (n == 36) return cpu_.cp0().cause();
    if (n == 37) return cpu_.pc();
    return 0;
}

void GdbStub::write_gdb_reg(int n, uint32_t value) {
    if (n >= 0 && n < 32) {
        cpu_.regs().write(static_cast<uint8_t>(n), value);
        return;
    }
    if (n == 32) {
        cpu_.cp0().write(Cp0::kRegStatus, value);
        return;
    }
    if (n == 33) {
        cpu_.set_lo(value);
        return;
    }
    if (n == 34) {
        cpu_.set_hi(value);
        return;
    }
    if (n == 36) {
        cpu_.cp0().write(Cp0::kRegCause, value);
        return;
    }
    if (n == 37) {
        cpu_.set_pc(value);
        return;
    }
}

// ─── Hex encoding helpers ─────────────────────────────────────────────────────

// Encode a 32-bit value in little-endian hex (8 chars, LSB first).
std::string GdbStub::to_hex_le(uint32_t v) {
    char buf[9];
    // Write bytes LSB-first.
    std::snprintf(buf, sizeof(buf), "%02x%02x%02x%02x", v & 0xFFu, (v >> 8) & 0xFFu,
                  (v >> 16) & 0xFFu, (v >> 24) & 0xFFu);
    return {buf};
}

std::optional<uint32_t> GdbStub::parse_hex(const std::string& s) {
    if (s.empty()) return std::nullopt;
    uint32_t v         = 0;
    const auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v, 16);
    // from_chars accepts neither sign nor "0x" prefix and never throws; require
    // the whole string to be consumed so trailing garbage is rejected.
    if (ec != std::errc{} || p != s.data() + s.size()) return std::nullopt;
    return v;
}

std::optional<uint8_t> GdbStub::parse_hex_byte(const std::string& s, std::size_t off) {
    if (off + 2 > s.size()) return std::nullopt;
    uint32_t    v      = 0;
    const char* first  = s.data() + off;
    const auto [p, ec] = std::from_chars(first, first + 2, v, 16);
    if (ec != std::errc{} || p != first + 2) return std::nullopt;
    return static_cast<uint8_t>(v);
}

// ─── Stop signal ─────────────────────────────────────────────────────────────

int GdbStub::stop_signal() const {
    if (last_result_ == StepResult::Halt) return kSIGTRAP;
    if (last_result_ == StepResult::Exception) {
        switch (cpu_.cp0().last_exception()) {
        case ExceptionCode::Bp:
            return kSIGTRAP;
        case ExceptionCode::Sys:
            return kSIGSYS;
        case ExceptionCode::RI:
            return kSIGILL;
        case ExceptionCode::Ov:
            return kSIGFPE;
        case ExceptionCode::AdEL:
        case ExceptionCode::AdES:
            return kSIGSEGV;
        default:
            return kSIGTRAP;
        }
    }
    return kSIGTRAP;
}

// ─── RSP command handlers ─────────────────────────────────────────────────────

std::string GdbStub::handle_read_regs() {
    std::string out;
    out.reserve(static_cast<std::size_t>(kNumRegs) * 8);
    for (int i = 0; i < kNumRegs; ++i)
        out += to_hex_le(read_gdb_reg(i));
    return out;
}

bool GdbStub::handle_write_regs(const std::string& hex) {
    if (hex.size() < static_cast<std::size_t>(kNumRegs) * 8) return false;
    for (int i = 0; i < kNumRegs; ++i) {
        // Little-endian: LSB is at the lowest address in the hex string.
        uint32_t v = 0;
        for (int b = 0; b < 4; ++b) {
            const auto byte =
                parse_hex_byte(hex, static_cast<size_t>(i) * 8 + static_cast<size_t>(b) * 2);
            if (!byte) return false;
            v |= static_cast<uint32_t>(*byte) << (b * 8);
        }
        write_gdb_reg(i, v);
    }
    return true;
}

std::string GdbStub::handle_read_reg(const std::string& args) {
    const auto n = parse_hex(args);
    if (!n || *n >= static_cast<uint32_t>(kNumRegs)) return "E01";
    return to_hex_le(read_gdb_reg(static_cast<int>(*n)));
}

bool GdbStub::handle_write_reg(const std::string& args) {
    const size_t eq = args.find('=');
    if (eq == std::string::npos) return false;
    const auto n = parse_hex(args.substr(0, eq));
    if (!n || *n >= static_cast<uint32_t>(kNumRegs)) return false;
    const std::string vs = args.substr(eq + 1);
    uint32_t          v  = 0;
    // GDB may send fewer than 4 bytes; consume what is present, stop at the first
    // incomplete/invalid byte.
    for (int b = 0; b < 4; ++b) {
        const auto byte = parse_hex_byte(vs, static_cast<size_t>(b) * 2);
        if (!byte) break;
        v |= static_cast<uint32_t>(*byte) << (b * 8);
    }
    write_gdb_reg(static_cast<int>(*n), v);
    return true;
}

std::string GdbStub::handle_read_mem(const std::string& args) {
    const size_t comma = args.find(',');
    if (comma == std::string::npos) return "E01";
    const auto addr = parse_hex(args.substr(0, comma));
    const auto len  = parse_hex(args.substr(comma + 1));
    if (!addr || !len) return "E01";
    // Clamp the requested length to the physical memory size: `len` is
    // attacker-controlled, and reserving `len * 2` for a bogus value would
    // otherwise drive a multi-gigabyte allocation before the bounds check runs.
    const uint32_t n = std::min<uint32_t>(*len, static_cast<uint32_t>(cpu_.mem().size()));
    std::string    out;
    out.reserve(static_cast<size_t>(n) * 2);
    for (uint32_t i = 0; i < n; ++i) {
        const auto b = cpu_.mem().read_byte(*addr + i);
        if (!b) return "E02";  // OOB
        out += hex_byte(*b);
    }
    return out;
}

bool GdbStub::handle_write_mem(const std::string& args) {
    const size_t colon = args.find(':');
    const size_t comma = args.find(',');
    if (colon == std::string::npos || comma == std::string::npos || comma > colon) return false;
    const auto addr = parse_hex(args.substr(0, comma));
    const auto len  = parse_hex(args.substr(comma + 1, colon - comma - 1));
    if (!addr || !len) return false;
    const std::string hex = args.substr(colon + 1);
    for (uint32_t i = 0; i < *len; ++i) {
        const auto byte = parse_hex_byte(hex, static_cast<size_t>(i) * 2);
        if (!byte) return false;  // truncated or non-hex payload
        if (!cpu_.mem().write_byte(*addr + i, *byte)) return false;
    }
    return true;
}

// ─── Breakpoints ─────────────────────────────────────────────────────────────

bool GdbStub::insert_breakpoint(uint32_t addr) {
    // Don't double-insert.
    for (const auto& bp : breakpoints_)
        if (bp.addr == addr) return true;

    const auto word = cpu_.mem().read_word(addr);
    if (!word) return false;

    if (!cpu_.mem().write_word(addr, kBreakWord)) return false;

    breakpoints_.push_back({addr, *word});
    return true;
}

bool GdbStub::remove_breakpoint(uint32_t addr) {
    for (auto it = breakpoints_.begin(); it != breakpoints_.end(); ++it) {
        if (it->addr == addr) {
            cpu_.mem().write_word(addr, it->saved_word);
            breakpoints_.erase(it);
            return true;
        }
    }
    return false;
}

void GdbStub::handle_breakpoint_set(const std::string& args) {
    // args: "type,addr,kind" — we only handle type 0 (software).
    const size_t c1 = args.find(',');
    if (c1 == std::string::npos) {
        send_error(1);
        return;
    }
    const size_t c2 = args.find(',', c1 + 1);
    if (c2 == std::string::npos) {
        send_error(1);
        return;
    }
    const auto type = parse_hex(args.substr(0, c1));
    if (!type) {
        send_error(1);
        return;
    }
    if (*type != 0) {
        send_empty();
        return;
    }  // unsupported breakpoint type
    const auto addr = parse_hex(args.substr(c1 + 1, c2 - c1 - 1));
    if (!addr) {
        send_error(1);
        return;
    }
    insert_breakpoint(*addr) ? send_ok() : send_error(1);
}

void GdbStub::handle_breakpoint_clear(const std::string& args) {
    const size_t c1 = args.find(',');
    if (c1 == std::string::npos) {
        send_error(1);
        return;
    }
    const size_t c2 = args.find(',', c1 + 1);
    if (c2 == std::string::npos) {
        send_error(1);
        return;
    }
    const auto type = parse_hex(args.substr(0, c1));
    if (!type) {
        send_error(1);
        return;
    }
    if (*type != 0) {
        send_empty();
        return;
    }
    const auto addr = parse_hex(args.substr(c1 + 1, c2 - c1 - 1));
    if (!addr) {
        send_error(1);
        return;
    }
    remove_breakpoint(*addr) ? send_ok() : send_error(1);
}

// ─── Continue / step ──────────────────────────────────────────────────────────

void GdbStub::handle_continue(const std::string& args) {
    if (!args.empty()) {
        if (const auto pc = parse_hex(args)) cpu_.set_pc(*pc);
    }

    running_          = true;
    std::size_t steps = 0;
    while (running_) {
        // The stub cannot read GDB's Ctrl-C while it only steps the CPU, so a
        // program that never halts would hang it. Check the socket now and then.
        if (++steps % kInterruptPollSteps == 0) {
            const InputPoll input = poll_input();
            if (input == InputPoll::Interrupt) {
                running_ = false;
                send_signal(kSIGINT);
                return;
            }
            if (input == InputPoll::Closed) {
                running_ = false;  // nobody is left to report a stop to
                return;
            }
        }
        last_result_ = cpu_.step();
        if (last_result_ == StepResult::Halt) {
            running_ = false;
            send_signal(kSIGTRAP);
            return;
        }
        if (last_result_ == StepResult::Exception) {
            running_ = false;
            // Report the faulting PC (EPC), not the exception vector.
            // GDB receives: T{sig}thread:01;
            char buf[32];
            std::snprintf(buf, sizeof(buf), "T%02xthread:01;", stop_signal());
            send_packet(buf);
            // Restore PC to EPC so GDB sees the instruction that caused the exception.
            cpu_.set_pc(cpu_.cp0().epc());
            return;
        }
        if (last_result_ == StepResult::Fault) {
            running_ = false;
            send_signal(kSIGTRAP);
            return;
        }
        // Check if we hit a software breakpoint (BREAK at current PC was restored;
        // the CPU already executed it and took a Bp exception above, so this path
        // is for hardware-BP-style address matching on normal instructions).
        // Nothing to do here — the BREAK word in memory handles it automatically.
    }
}

void GdbStub::handle_step(const std::string& args) {
    if (!args.empty()) {
        if (const auto pc = parse_hex(args)) cpu_.set_pc(*pc);
    }

    last_result_ = cpu_.step();
    if (last_result_ == StepResult::Exception) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "T%02xthread:01;", stop_signal());
        send_packet(buf);
        cpu_.set_pc(cpu_.cp0().epc());
        return;
    }
    send_signal(kSIGTRAP);
}

// ─── Main dispatch ────────────────────────────────────────────────────────────

void GdbStub::dispatch(const std::string& pkt) {
    if (pkt.empty()) {
        send_empty();
        return;
    }

    const char        cmd  = pkt[0];
    const std::string args = pkt.substr(1);

    switch (cmd) {
    case '?':
        send_signal(kSIGTRAP);
        break;

    case 'g':
        send_packet(handle_read_regs());
        break;

    case 'G':
        handle_write_regs(args) ? send_ok() : send_error(1);
        break;

    case 'p':
        send_packet(handle_read_reg(args));
        break;

    case 'P':
        handle_write_reg(args) ? send_ok() : send_error(1);
        break;

    case 'm':
        send_packet(handle_read_mem(args));
        break;

    case 'M':
        handle_write_mem(args) ? send_ok() : send_error(1);
        break;

    case 'c':
        handle_continue(args);
        break;

    case 's':
        handle_step(args);
        break;

    case 'Z':
        handle_breakpoint_set(args);
        break;

    case 'z':
        handle_breakpoint_clear(args);
        break;

    case 'k':  // kill — end the session
    case 'D':  // detach — end the session but leave the program as it is
        running_  = false;
        detached_ = true;
        send_ok();
        break;

    case 'H':  // set thread — there is one thread, so any selection is fine
    case 'T':  // thread alive — the one thread always is
        send_ok();
        break;

    case 'q':
        if (args.starts_with("Supported")) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "PacketSize=%zx;swbreak+;hwbreak-", kMaxPacketSize);
            send_packet(buf);
        } else if (args == "Attached") {
            send_packet("1");  // attached to existing process
        } else if (args == "C") {
            send_packet("QC0");
        } else if (args.starts_with("Symbol")) {
            send_ok();
        } else {
            send_empty();
        }
        break;

    // vCont? and the other v-packets are unsupported; GDB falls back to 'c'/'s'.
    // An empty reply is "unsupported" for every other packet too.
    case 'v':
    default:
        send_empty();
        break;
    }
}

}  // namespace mips
