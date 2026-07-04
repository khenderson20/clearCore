#include "mips/gdb_stub.h"
#include "mips/cp0.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <gsl/gsl>
#include <iomanip>
#include <sstream>
#include <string>

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
static constexpr int kSIGTRAP = 5;
static constexpr int kSIGSEGV = 11;
static constexpr int kSIGILL  = 4;
static constexpr int kSIGFPE  = 8;
static constexpr int kSIGSYS  = 12;

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

    // Disable Nagle — RSP is request-response, latency matters more than throughput.
    ::setsockopt(client_fd_, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));

    // RSP event loop.
    last_result_ = StepResult::Ok;
    std::string pkt;
    while (recv_packet(pkt)) {
        dispatch(pkt);
    }
}

// ─── Packet I/O ──────────────────────────────────────────────────────────────

uint8_t GdbStub::checksum(const std::string& data) noexcept {
    uint8_t sum = 0;
    for (unsigned char c : data)
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
        const ssize_t n = ::send(client_fd_, p, static_cast<size_t>(rem), 0);
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

bool GdbStub::recv_packet(std::string& out) {
    out.clear();
    // Skip until '$'.
    char c;
    while (true) {
        const ssize_t n = ::recv(client_fd_, &c, 1, 0);
        if (n <= 0) return false;
        if (c == '$') break;
        if (c == '\x03') {
            // Ctrl-C interrupt: treat like a step + stop.
            send_signal(kSIGTRAP);
            return true;
        }
    }
    // Read until '#'.
    while (true) {
        const ssize_t n = ::recv(client_fd_, &c, 1, 0);
        if (n <= 0) return false;
        if (c == '#') break;
        out += c;
    }
    // Read two-character checksum (we accept it without verifying for simplicity).
    char cksum[2];
    if (::recv(client_fd_, cksum, 2, MSG_WAITALL) != 2) return false;
    send_raw("+");  // ACK
    return true;
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

uint32_t GdbStub::parse_hex(const std::string& s) {
    return static_cast<uint32_t>(std::stoul(s, nullptr, 16));
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
    out.reserve(kNumRegs * 8);
    for (int i = 0; i < kNumRegs; ++i)
        out += to_hex_le(read_gdb_reg(i));
    return out;
}

bool GdbStub::handle_write_regs(const std::string& hex) {
    if (hex.size() < static_cast<size_t>(kNumRegs * 8)) return false;
    for (int i = 0; i < kNumRegs; ++i) {
        const std::string chunk = hex.substr(static_cast<size_t>(i) * 8, 8);
        // Little-endian: LSB is at the lowest address in the hex string.
        uint32_t v = 0;
        for (int b = 0; b < 4; ++b) {
            const std::string byte_str = chunk.substr(static_cast<size_t>(b) * 2, 2);
            v |= static_cast<uint32_t>(std::stoul(byte_str, nullptr, 16)) << (b * 8);
        }
        write_gdb_reg(i, v);
    }
    return true;
}

std::string GdbStub::handle_read_reg(const std::string& args) {
    const int n = static_cast<int>(parse_hex(args));
    if (n >= kNumRegs) return "E01";
    return to_hex_le(read_gdb_reg(n));
}

bool GdbStub::handle_write_reg(const std::string& args) {
    const size_t eq = args.find('=');
    if (eq == std::string::npos) return false;
    const int n = static_cast<int>(parse_hex(args.substr(0, eq)));
    if (n >= kNumRegs) return false;
    const std::string& vs = args.substr(eq + 1);
    uint32_t           v  = 0;
    for (int b = 0; b < 4 && b * 2 + 1 < static_cast<int>(vs.size()); ++b)
        v |=
            static_cast<uint32_t>(std::stoul(vs.substr(static_cast<size_t>(b) * 2, 2), nullptr, 16))
            << (b * 8);
    write_gdb_reg(n, v);
    return true;
}

std::string GdbStub::handle_read_mem(const std::string& args) {
    const size_t comma = args.find(',');
    if (comma == std::string::npos) return "E01";
    const uint32_t addr = parse_hex(args.substr(0, comma));
    const uint32_t len  = parse_hex(args.substr(comma + 1));
    std::string    out;
    out.reserve(len * 2);
    for (uint32_t i = 0; i < len; ++i) {
        const auto b = cpu_.mem().read_byte(addr + i);
        if (!b) return "E02";  // OOB
        out += hex_byte(*b);
    }
    return out;
}

bool GdbStub::handle_write_mem(const std::string& args) {
    const size_t colon = args.find(':');
    const size_t comma = args.find(',');
    if (colon == std::string::npos || comma == std::string::npos) return false;
    const uint32_t     addr = parse_hex(args.substr(0, comma));
    const uint32_t     len  = parse_hex(args.substr(comma + 1, colon - comma - 1));
    const std::string& hex  = args.substr(colon + 1);
    for (uint32_t i = 0; i < len; ++i) {
        if (i * 2 + 1 >= hex.size()) return false;
        const uint8_t byte = static_cast<uint8_t>(
            std::stoul(hex.substr(static_cast<size_t>(i) * 2, 2), nullptr, 16));
        if (!cpu_.mem().write_byte(addr + i, byte)) return false;
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
    const int type = static_cast<int>(parse_hex(args.substr(0, c1)));
    if (type != 0) {
        send_empty();
        return;
    }  // unsupported breakpoint type
    const uint32_t addr = parse_hex(args.substr(c1 + 1, c2 - c1 - 1));
    insert_breakpoint(addr) ? send_ok() : send_error(1);
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
    const int type = static_cast<int>(parse_hex(args.substr(0, c1)));
    if (type != 0) {
        send_empty();
        return;
    }
    const uint32_t addr = parse_hex(args.substr(c1 + 1, c2 - c1 - 1));
    remove_breakpoint(addr) ? send_ok() : send_error(1);
}

// ─── Continue / step ──────────────────────────────────────────────────────────

void GdbStub::handle_continue(const std::string& args) {
    if (!args.empty()) cpu_.set_pc(parse_hex(args));

    running_ = true;
    while (running_) {
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
    if (!args.empty()) cpu_.set_pc(parse_hex(args));

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

    case 'k':  // kill — stop the loop
        running_ = false;
        send_ok();
        break;

    case 'D':  // detach — stop the loop but don't kill the program
        running_ = false;
        send_ok();
        break;

    case 'H':  // set thread — we have one thread; ignore
        send_ok();
        break;

    case 'T':  // thread alive
        send_ok();
        break;

    case 'q':
        if (args.substr(0, 9) == "Supported") {
            send_packet("PacketSize=4000;swbreak+;hwbreak-");
        } else if (args == "Attached") {
            send_packet("1");  // attached to existing process
        } else if (args == "C") {
            send_packet("QC0");
        } else if (args.substr(0, 6) == "Symbol") {
            send_ok();
        } else {
            send_empty();
        }
        break;

    case 'v':
        if (args.substr(0, 5) == "Cont?") {
            send_empty();  // not supported; GDB falls back to 'c'/'s'
        } else {
            send_empty();
        }
        break;

    default:
        send_empty();
        break;
    }
}

}  // namespace mips
