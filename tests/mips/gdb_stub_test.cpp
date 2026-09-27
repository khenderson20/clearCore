// GDB stub robustness tests (#125).
//
// The RSP handlers parse attacker-controllable hex fields from packets whose
// checksum is intentionally unverified. Previously they delegated to std::stoul,
// which throws std::invalid_argument / std::out_of_range on malformed or
// oversized input; nothing between the handler and the packet loop caught it, so
// a single bad packet aborted the emulator. These tests pin the replacement
// parsers' contract: valid → value, everything else → nullopt, never throw.

#include "mips/gdb_stub.h"

#include <cstdint>
#include <cstdio>
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
};

}  // namespace mips

int main() {
    using mips::GdbStubTestAccess;

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

    if (g_failed == 0) {
        std::printf("All %d gdb_stub tests passed.\n", g_passed);
        return 0;
    }
    std::printf("%d of %d gdb_stub tests failed.\n", g_failed, g_passed + g_failed);
    return 1;
}
