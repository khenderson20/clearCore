// GDB stub robustness tests (#125).
//
// The RSP handlers parse attacker-controllable hex fields from packet payloads.
// Previously they delegated to std::stoul,
// which throws std::invalid_argument / std::out_of_range on malformed or
// oversized input; nothing between the handler and the packet loop caught it, so
// a single bad packet aborted the emulator. These tests pin the replacement
// parsers' contract: valid → value, everything else → nullopt, never throw.

#include "mips/gdb_stub.h"
#include "mips/single_cycle_cpu.h"

#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>

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
    static void set_client_fd(GdbStub& stub, int fd) { stub.client_fd_ = fd; }
    static bool recv_packet(GdbStub& stub, std::string& out) { return stub.recv_packet(out); }
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
    // The stub has already written its reply before recv_packet returns, so this never blocks
    // for long; a short read means the stub sent less than expected.
    std::string receive(std::size_t n) const {
        std::string buf(n, '\0');
        const auto  got = ::recv(peer, buf.data(), n, MSG_WAITALL);
        buf.resize(got > 0 ? static_cast<std::size_t>(got) : 0);
        return buf;
    }
    void close_write() const { ::shutdown(peer, SHUT_WR); }
};

}  // namespace mips

int main() {
    using mips::GdbStubTestAccess;
    using mips::StubSession;

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
        CHECK(GdbStubTestAccess::recv_packet(t.stub, out));
        CHECK(out == "OK");
        CHECK(t.receive(1) == "+");
    }
    {  // bad checksum → '-' sent, retransmission accepted, '+' sent
        StubSession t;
        std::string out;
        t.send("$OK#00$OK#9a");
        CHECK(GdbStubTestAccess::recv_packet(t.stub, out));
        CHECK(out == "OK");  // the corrupted payload was discarded, not accumulated
        CHECK(t.receive(2) == "-+");
    }
    {  // non-hex checksum digits are also a NAK
        StubSession t;
        std::string out;
        t.send("$OK#zz$g#67");
        CHECK(GdbStubTestAccess::recv_packet(t.stub, out));
        CHECK(out == "g");
        CHECK(t.receive(2) == "-+");
    }
    {  // Ctrl-C interrupt → SIGTRAP stop reply, no ACK
        StubSession t;
        std::string out;
        t.send("");
        CHECK(GdbStubTestAccess::recv_packet(t.stub, out));
        CHECK(t.receive(7) == "$S05#b8");
    }
    {  // connection drops before '$'
        StubSession t;
        std::string out;
        t.close_write();
        CHECK(!GdbStubTestAccess::recv_packet(t.stub, out));
    }
    {  // connection drops mid-payload
        StubSession t;
        std::string out;
        t.send("$OK");
        t.close_write();
        CHECK(!GdbStubTestAccess::recv_packet(t.stub, out));
    }
    {  // connection drops inside the checksum
        StubSession t;
        std::string out;
        t.send("$OK#9");
        t.close_write();
        CHECK(!GdbStubTestAccess::recv_packet(t.stub, out));
    }
    {  // NAK'd packet followed by a drop: gives up instead of spinning
        StubSession t;
        std::string out;
        t.send("$OK#00");
        t.close_write();
        CHECK(!GdbStubTestAccess::recv_packet(t.stub, out));
        CHECK(t.receive(1) == "-");
    }

    if (g_failed == 0) {
        std::printf("All %d gdb_stub tests passed.\n", g_passed);
        return 0;
    }
    std::printf("%d of %d gdb_stub tests failed.\n", g_failed, g_passed + g_failed);
    return 1;
}
