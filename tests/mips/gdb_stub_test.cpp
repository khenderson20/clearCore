// GDB stub robustness tests (#125, #219).
//
// The RSP handlers parse attacker-controllable hex fields from packet payloads.
// Previously they delegated to std::stoul,
// which throws std::invalid_argument / std::out_of_range on malformed or
// oversized input; nothing between the handler and the packet loop caught it, so
// a single bad packet aborted the emulator. These tests pin the replacement
// parsers' contract: valid → value, everything else → nullopt, never throw.
//
// The session tests drive the real packet loop over a socketpair: Ctrl-C gets
// exactly one stop reply, a running 'c' can be interrupted, oversized and
// malformed packets are NAKed, escapes are decoded, and 'D' ends the session.

#include "mips/gdb_stub.h"
#include "mips/single_cycle_cpu.h"

#include <sys/socket.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

static int g_passed = 0, g_failed = 0;

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (expr) {                                                                                \
            ++g_passed;                                                                            \
        } else {                                                                                   \
            std::fprintf(stderr, "FAIL  %s:%d  %s\n", __FILE__, __LINE__, #expr);                  \
            ++g_failed;                                                                            \
        }                                                                                          \
    } while (false)

namespace mips {

// Test seam: forwards to the private static parsers (see friend decl in gdb_stub.h).
struct GdbStubTestAccess {
    static std::optional<uint32_t> parse_hex(const std::string& s) { return GdbStub::parse_hex(s); }
    static std::optional<uint8_t>  parse_hex_byte(const std::string& s, std::size_t off) {
        return GdbStub::parse_hex_byte(s, off);
    }
    static bool checksum_matches(const std::string& d, char hi, char lo) {
        return GdbStub::checksum_matches(d, hi, lo);
    }
    static std::optional<std::string> unescape(const std::string& raw) {
        return GdbStub::unescape(raw);
    }
    static constexpr std::size_t kMaxPacketSize = GdbStub::kMaxPacketSize;

    static void set_client_fd(GdbStub& stub, int fd) {
        stub.client_fd_ = fd;
        stub.configure_client_socket();
    }
    // Packet: payload in `out`. Interrupt: a bare Ctrl-C. Closed: the connection dropped.
    enum class Recv : std::uint8_t { Packet, Interrupt, Closed };
    static Recv recv_packet(GdbStub& stub, std::string& out) {
        switch (stub.recv_packet(out)) {
        case GdbStub::RecvStatus::Packet:
            return Recv::Packet;
        case GdbStub::RecvStatus::Interrupt:
            return Recv::Interrupt;
        case GdbStub::RecvStatus::Closed:
            return Recv::Closed;
        }
        return Recv::Closed;
    }
    static bool send_packet(GdbStub& stub, const std::string& data) {
        return stub.send_packet(data);
    }
    static void serve(GdbStub& stub) { stub.serve(); }
};

// One end of a socketpair is handed to a stub as its "GDB client" connection (the stub
// closes it); the test drives the other end as the remote debugger.
struct StubSession {
    mips::SingleCycleCpu cpu;
    mips::GdbStub        stub{cpu};
    int                  peer = -1;

    StubSession() {
        int sv[2];
        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) std::abort();
        mips::GdbStubTestAccess::set_client_fd(stub, sv[0]);
        peer = sv[1];
    }
    ~StubSession() { ::close(peer); }

    void send(const std::string& bytes) const { ::send(peer, bytes.data(), bytes.size(), 0); }
    // For a frame larger than the socketpair buffer (8 KiB on macOS): send() blocks until the
    // stub reads, and the stub reads on this thread, so the write needs a thread of its own.
    [[nodiscard]] std::thread send_async(std::string bytes) const {
        return std::thread([this, bytes = std::move(bytes)] { send(bytes); });
    }
    // The stub has already written its reply before recv_packet returns, so this never blocks
    // for long; a short read means the stub sent less than expected.
    std::string receive(std::size_t n) const {
        std::string buf(n, '\0');
        const auto  got = ::recv(peer, buf.data(), n, MSG_WAITALL);
        buf.resize(got > 0 ? static_cast<std::size_t>(got) : 0);
        return buf;
    }
    // Everything the stub has written so far, without blocking.
    std::string drain() const {
        std::string all;
        char        buf[256];
        while (true) {
            const auto got = ::recv(peer, buf, sizeof(buf), MSG_DONTWAIT);
            if (got <= 0) break;
            all.append(buf, static_cast<std::size_t>(got));
        }
        return all;
    }
    void close_write() const { ::shutdown(peer, SHUT_WR); }
};

// "$<payload>#<checksum>" — the framing GDB puts on every packet it sends.
std::string frame(const std::string& payload) {
    unsigned sum = 0;
    for (const unsigned char c : payload)
        sum += c;
    char tail[4];
    std::snprintf(tail, sizeof(tail), "#%02x", sum & 0xFFu);
    return "$" + payload + tail;
}

}  // namespace mips

int main() {
    using mips::frame;
    using mips::GdbStubTestAccess;
    using mips::StubSession;
    using Recv = GdbStubTestAccess::Recv;

    // A regression in the interrupt path would block in recv() or spin in 'c' forever;
    // fail the test instead of hanging CI until its job timeout.
    ::alarm(30);

    // ── parse_hex: valid input ────────────────────────────────────────────────
    CHECK(GdbStubTestAccess::parse_hex("0").value_or(1) == 0u);
    CHECK(GdbStubTestAccess::parse_hex("ff").value_or(0) == 0xffu);
    CHECK(GdbStubTestAccess::parse_hex("FF").value_or(0) == 0xffu);
    CHECK(GdbStubTestAccess::parse_hex("deadbeef").value_or(0) == 0xdeadbeefu);
    CHECK(GdbStubTestAccess::parse_hex("ffffffff").value_or(0) == 0xffffffffu);

    // ── parse_hex: malformed input must be nullopt, not a thrown exception ─────
    CHECK(!GdbStubTestAccess::parse_hex("").has_value());           // empty
    CHECK(!GdbStubTestAccess::parse_hex("zz").has_value());         // non-hex
    CHECK(!GdbStubTestAccess::parse_hex("12g4").has_value());       // trailing garbage
    CHECK(!GdbStubTestAccess::parse_hex("0xFF").has_value());       // 0x prefix rejected
    CHECK(!GdbStubTestAccess::parse_hex("-1").has_value());         // sign rejected
    CHECK(!GdbStubTestAccess::parse_hex(" 4").has_value());         // leading whitespace
    CHECK(!GdbStubTestAccess::parse_hex("100000000").has_value());  // > 32 bits overflows

    // ── parse_hex_byte: exactly two hex digits at the given offset ─────────────
    CHECK(GdbStubTestAccess::parse_hex_byte("ab", 0).value_or(0) == 0xabu);
    CHECK(GdbStubTestAccess::parse_hex_byte("00ff", 2).value_or(0) == 0xffu);
    CHECK(!GdbStubTestAccess::parse_hex_byte("a", 0).has_value());   // too short
    CHECK(!GdbStubTestAccess::parse_hex_byte("ab", 2).has_value());  // offset past end
    CHECK(!GdbStubTestAccess::parse_hex_byte("zz", 0).has_value());  // non-hex
    CHECK(!GdbStubTestAccess::parse_hex_byte("0x", 0).has_value());  // prefix, not a byte

    // ── checksum_matches: mod-256 sum of the payload as two hex digits ─────────
    CHECK(GdbStubTestAccess::checksum_matches("OK", '9', 'a'));   // 'O'+'K' = 0x9a
    CHECK(GdbStubTestAccess::checksum_matches("OK", '9', 'A'));   // digits are case-insensitive
    CHECK(GdbStubTestAccess::checksum_matches("", '0', '0'));     // empty payload
    CHECK(!GdbStubTestAccess::checksum_matches("OK", '9', 'b'));  // off by one
    CHECK(!GdbStubTestAccess::checksum_matches("OK", '0', '0'));  // wrong value
    CHECK(!GdbStubTestAccess::checksum_matches("OK", 'z', 'z'));  // non-hex digits
    CHECK(!GdbStubTestAccess::checksum_matches("OK", '+', '9'));  // sign is not a digit

    // ── recv_packet: framing, ACK/NAK and retransmit over a real socket ────────
    {  // valid checksum → payload returned, '+' sent
        StubSession t;
        std::string out;
        t.send("$OK#9a");
        CHECK(GdbStubTestAccess::recv_packet(t.stub, out) == Recv::Packet);
        CHECK(out == "OK");
        CHECK(t.receive(1) == "+");
    }
    {  // bad checksum → '-' sent, retransmission accepted, '+' sent
        StubSession t;
        std::string out;
        t.send("$OK#00$OK#9a");
        CHECK(GdbStubTestAccess::recv_packet(t.stub, out) == Recv::Packet);
        CHECK(out == "OK");  // the corrupted payload was discarded, not accumulated
        CHECK(t.receive(2) == "-+");
    }
    {  // non-hex checksum digits are also a NAK
        StubSession t;
        std::string out;
        t.send("$OK#zz$g#67");
        CHECK(GdbStubTestAccess::recv_packet(t.stub, out) == Recv::Packet);
        CHECK(out == "g");
        CHECK(t.receive(2) == "-+");
    }
    {  // Ctrl-C between packets is reported as an interrupt; recv_packet itself replies nothing
        StubSession t;
        std::string out;
        t.send("\x03");
        CHECK(GdbStubTestAccess::recv_packet(t.stub, out) == Recv::Interrupt);
        CHECK(t.drain().empty());
    }
    {  // connection drops before '$'
        StubSession t;
        std::string out;
        t.close_write();
        CHECK(GdbStubTestAccess::recv_packet(t.stub, out) == Recv::Closed);
    }
    {  // connection drops mid-payload
        StubSession t;
        std::string out;
        t.send("$OK");
        t.close_write();
        CHECK(GdbStubTestAccess::recv_packet(t.stub, out) == Recv::Closed);
    }
    {  // connection drops inside the checksum
        StubSession t;
        std::string out;
        t.send("$OK#9");
        t.close_write();
        CHECK(GdbStubTestAccess::recv_packet(t.stub, out) == Recv::Closed);
    }
    {  // NAK'd packet followed by a drop: gives up instead of spinning
        StubSession t;
        std::string out;
        t.send("$OK#00");
        t.close_write();
        CHECK(GdbStubTestAccess::recv_packet(t.stub, out) == Recv::Closed);
        CHECK(t.receive(1) == "-");
    }

    // ── unescape: '}' x means x ^ 0x20 ─────────────────────────────────────────
    CHECK(GdbStubTestAccess::unescape("plain").value_or("") == "plain");
    CHECK(GdbStubTestAccess::unescape("}]").value_or("") == "}");     // 0x5d ^ 0x20 = '}'
    CHECK(GdbStubTestAccess::unescape("}\x03").value_or("") == "#");  // 0x03 ^ 0x20 = '#'
    CHECK(GdbStubTestAccess::unescape("}\x04").value_or("") == "$");  // 0x04 ^ 0x20 = '$'
    CHECK(GdbStubTestAccess::unescape("}\x0a").value_or("") == "*");  // 0x0a ^ 0x20 = '*'
    CHECK(!GdbStubTestAccess::unescape("}").has_value());             // escape of nothing
    CHECK(!GdbStubTestAccess::unescape("ab}").has_value());

    {  // an escaped payload is decoded after its (escaped) checksum passes
        StubSession t;
        std::string out;
        t.send(frame("a}]b"));
        CHECK(GdbStubTestAccess::recv_packet(t.stub, out) == Recv::Packet);
        CHECK(out == "a}b");
        CHECK(t.receive(1) == "+");
    }
    {  // a payload that ends inside an escape is NAKed even with a good checksum
        StubSession t;
        std::string out;
        t.send(frame("a}") + frame("OK"));
        CHECK(GdbStubTestAccess::recv_packet(t.stub, out) == Recv::Packet);
        CHECK(out == "OK");
        CHECK(t.receive(2) == "-+");
    }
    {  // a payload over the advertised PacketSize is NAKed, not buffered without bound
        StubSession t;
        std::string out;
        auto writer = t.send_async(frame(std::string(GdbStubTestAccess::kMaxPacketSize + 1, 'a')) +
                                   frame("OK"));
        CHECK(GdbStubTestAccess::recv_packet(t.stub, out) == Recv::Packet);
        writer.join();
        CHECK(out == "OK");
        CHECK(t.receive(2) == "-+");
    }
    {  // exactly PacketSize is still accepted
        StubSession       t;
        std::string       out;
        const std::string big(GdbStubTestAccess::kMaxPacketSize, 'a');
        auto              writer = t.send_async(frame(big));
        CHECK(GdbStubTestAccess::recv_packet(t.stub, out) == Recv::Packet);
        writer.join();
        CHECK(out == big);
        CHECK(t.receive(1) == "+");
    }

    // ── serve(): whole-session behaviour ─────────────────────────────────────
    {  // Ctrl-C while stopped → exactly one SIGINT stop reply, no empty "$#00" after it
        StubSession t;
        t.send("\x03");
        t.close_write();
        GdbStubTestAccess::serve(t.stub);
        CHECK(t.drain() == "$S02#b5");
    }
    {  // qSupported advertises the same limit the receive path enforces
        StubSession t;
        t.send(frame("qSupported"));
        t.close_write();
        GdbStubTestAccess::serve(t.stub);
        CHECK(t.drain() == "+" + frame("PacketSize=4000;swbreak+;hwbreak-"));
    }
    {  // 'c' on a program that never halts stops on Ctrl-C with a SIGINT stop reply
        StubSession t;
        // 0x0: addi $t0, $t0, 1   0x4: j 0x0 — a loop, not the "j self" halt idiom
        CHECK(t.cpu.load_program({0x2108'0001u, 0x0800'0000u}));
        // The connection stays open, so only the poll inside 'c' can see the Ctrl-C;
        // the trailing 'D' then ends the session.
        t.send(frame("c") + "\x03" + frame("D"));
        GdbStubTestAccess::serve(t.stub);
        CHECK(t.drain() == "+$S02#b5+$OK#9a");
        CHECK(t.cpu.pc() < 8);             // still inside the loop
        CHECK(t.cpu.regs().read(8) > 0u);  // it really ran before the poll saw Ctrl-C
    }
    {  // Ctrl-C followed by a hang-up still interrupts the running 'c'
        StubSession t;
        CHECK(t.cpu.load_program({0x2108'0001u, 0x0800'0000u}));
        t.send(frame("c") + "\x03");
        t.close_write();
        GdbStubTestAccess::serve(t.stub);
        CHECK(t.drain() == "+$S02#b5");
    }
    {  // 'D' ends the session even though the connection stays open
        StubSession t;
        t.send(frame("D"));
        GdbStubTestAccess::serve(t.stub);  // would block in recv() if 'D' did not end the loop
        CHECK(t.drain() == "+$OK#9a");
    }
    {  // a reply to a peer that has gone away fails instead of raising SIGPIPE
        StubSession t;
        ::close(t.peer);
        t.peer = -1;
        CHECK(!GdbStubTestAccess::send_packet(t.stub, "OK"));
    }

    if (g_failed == 0) {
        std::printf("All %d gdb_stub tests passed.\n", g_passed);
        return 0;
    }
    std::printf("%d of %d gdb_stub tests failed.\n", g_failed, g_passed + g_failed);
    return 1;
}
