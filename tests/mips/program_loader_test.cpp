// program_loader_test.cpp — unit tests for the hex program loader.
//
// Split out of disasm_test.cpp (#172): these exercise src/mips/program_loader.cpp,
// a different translation unit from the disassembler, and a loader regression
// reported as `disasm_test` failing sends you to the wrong file.
//
// Lightweight harness — no external dependencies. Build via CMake target
// program_loader_test, or directly:
//   g++ -std=c++20 -Iinclude src/mips/*.cpp tests/mips/program_loader_test.cpp -o loader_test

#include "mips/program_loader.h"

#include <cstddef>
#include <cstdio>
#include <sstream>
#include <string>

using namespace mips;

static int g_passed = 0, g_failed = 0;

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (expr) {                                                                                \
            ++g_passed;                                                                            \
        } else {                                                                                   \
            ++g_failed;                                                                            \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr);                          \
        }                                                                                          \
    } while (0)

#define CHECK_EQ(a, b)                                                                             \
    do {                                                                                           \
        const auto va_ = (a);                                                                      \
        const auto vb_ = (b);                                                                      \
        if (va_ == vb_) {                                                                          \
            ++g_passed;                                                                            \
        } else {                                                                                   \
            ++g_failed;                                                                            \
            std::printf("  FAIL %s:%d  %s != %s\n", __FILE__, __LINE__, #a, #b);                   \
        }                                                                                          \
    } while (0)

// ── Program-loader tests ──────────────────────────────────────────────────────
static void test_loader_basic() {
    std::istringstream in("0x00000020\n0xDEADBEEF\n08\n");
    const HexProgram   p = parse_hex_program(in);
    CHECK(p.ok());
    CHECK_EQ(p.words.size(), std::size_t{3});
    CHECK_EQ(p.words[0], 0x0000'0020u);
    CHECK_EQ(p.words[1], 0xDEAD'BEEFu);
    CHECK_EQ(p.words[2], 0x0000'0008u);
}

static void test_loader_comments_and_blanks() {
    std::istringstream in("# header comment\n"
                          "\n"
                          "  0x10   # inline comment\n"
                          "   \n"
                          "20\n");
    const HexProgram   p = parse_hex_program(in);
    CHECK(p.ok());
    CHECK_EQ(p.words.size(), std::size_t{2});
    CHECK_EQ(p.words[0], 0x10u);
    CHECK_EQ(p.words[1], 0x20u);
}

static void test_loader_bad_hex_reports_line() {
    std::istringstream in("0x01\n"
                          "not_hex\n"
                          "0x03\n");
    const HexProgram   p = parse_hex_program(in);
    CHECK(!p.ok());
    CHECK(p.words.empty());
    CHECK(p.error.has_value());
    CHECK(p.error->find("line 2") != std::string::npos);
}

static void test_loader_trailing_garbage_rejected() {
    // "12xy" must not silently parse as 0x12 — the whole token must be hex.
    std::istringstream in("12xy\n");
    const HexProgram   p = parse_hex_program(in);
    CHECK(!p.ok());
}

static void test_loader_full_width_word_accepted() {
    // 0xFFFFFFFF is the widest valid 32-bit word — must parse on every platform.
    std::istringstream in("0xFFFFFFFF\nFFFFFFFF\n");
    const HexProgram   p = parse_hex_program(in);
    CHECK(p.ok());
    CHECK_EQ(p.words.size(), std::size_t{2});
    CHECK_EQ(p.words[0], 0xFFFF'FFFFu);
    CHECK_EQ(p.words[1], 0xFFFF'FFFFu);
}

static void test_loader_overwide_word_rejected() {
    // Regression: std::stoul parsed this into a 64-bit `unsigned long` on LP64
    // (Linux/macOS) and the cast to uint32_t silently truncated it to
    // 0xFFFFFFFF, while on Windows (LLP64, 32-bit long) it threw out_of_range
    // and was rejected. Must now be rejected everywhere.
    std::istringstream in("1FFFFFFFF\n");
    const HexProgram   p = parse_hex_program(in);
    CHECK(!p.ok());
    CHECK(p.words.empty());
    CHECK(p.error.has_value());
    CHECK(p.error->find("line 1") != std::string::npos);
}

static void test_loader_negative_rejected() {
    // std::stoul accepted a leading '-' and wrapped it; a hex word listing has
    // no sign, so it must be a parse error.
    std::istringstream in("-1\n");
    const HexProgram   p = parse_hex_program(in);
    CHECK(!p.ok());
    CHECK(p.words.empty());
}

static void test_loader_bare_prefix_rejected() {
    // "0x" with no digits is not a word.
    std::istringstream in("0x\n");
    const HexProgram   p = parse_hex_program(in);
    CHECK(!p.ok());
}

static void test_loader_empty_is_valid() {
    std::istringstream in("# only comments\n\n");
    const HexProgram   p = parse_hex_program(in);
    CHECK(p.ok());
    CHECK(p.words.empty());
}

int main() {
    test_loader_basic();
    test_loader_comments_and_blanks();
    test_loader_bad_hex_reports_line();
    test_loader_trailing_garbage_rejected();
    test_loader_full_width_word_accepted();
    test_loader_overwide_word_rejected();
    test_loader_negative_rejected();
    test_loader_bare_prefix_rejected();
    test_loader_empty_is_valid();

    std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
