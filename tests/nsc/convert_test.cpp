// Minimal, dependency-free test harness for the pure conversion core.
//
// These assertions encode the documented contract. They will FAIL until you
// implement the functions — that red-to-green loop is the intended workflow.
// Run with: ctest --test-dir cmake-build-debug   (or ./cmake-build-debug/nsc_tests)

#include <cstdint>
#include <cstdio>
#include <string>

#include "nsc/converter.h"
#include "nsc/format.h"
#include "nsc/parse.h"

namespace {

int g_failures = 0;

template <typename A, typename B>
void expectEq(const A& got, const B& want, const char* expr, int line) {
    if (!(got == want)) {
        ++g_failures;
        std::printf("FAIL (line %d): %s\n", line, expr);
    }
}

#define EXPECT_EQ(got, want) expectEq((got), (want), #got " == " #want, __LINE__)

void parseTests() {
    EXPECT_EQ(nsc::parseBase("0", 10).value_or(1), std::uint64_t{0});
    EXPECT_EQ(nsc::parseBase("255", 10).value_or(0), std::uint64_t{255});
    EXPECT_EQ(nsc::parseBase("FF", 16).value_or(0), std::uint64_t{255});
    EXPECT_EQ(nsc::parseBase("11111111", 2).value_or(0), std::uint64_t{255});
    EXPECT_EQ(nsc::parseBase("", 10).has_value(), false);     // empty
    EXPECT_EQ(nsc::parseBase("12x", 10).has_value(), false);  // trailing garbage
    EXPECT_EQ(nsc::parseBase("2", 2).has_value(), false);     // not a binary digit

    // Regression (#124): stoull-lenient inputs the contract requires rejecting.
    EXPECT_EQ(nsc::parseBase("-1", 10).has_value(), false);    // negative sign
    EXPECT_EQ(nsc::parseBase("-0", 10).has_value(), false);    // negative zero
    EXPECT_EQ(nsc::parseBase("+5", 10).has_value(), false);    // positive sign
    EXPECT_EQ(nsc::parseBase(" 7", 10).has_value(), false);    // leading whitespace
    EXPECT_EQ(nsc::parseBase("7 ", 10).has_value(), false);    // trailing whitespace
    EXPECT_EQ(nsc::parseBase("0xFF", 16).has_value(), false);  // 0x prefix (base 16)
    // Overflow past 64 bits must still be rejected.
    EXPECT_EQ(nsc::parseBase("18446744073709551616", 10).has_value(), false);
    // Valid inputs across bases remain accepted.
    EXPECT_EQ(nsc::parseBase("FF", 16).value_or(0), std::uint64_t{255});
    EXPECT_EQ(nsc::parseBase("ff", 16).value_or(0), std::uint64_t{255});
    EXPECT_EQ(nsc::parseBase("18446744073709551615", 10).value_or(0),
              std::uint64_t{18446744073709551615ULL});
}

void formatTests() {
    EXPECT_EQ(nsc::toBinary(0), std::string{"0"});
    EXPECT_EQ(nsc::toBinary(245), std::string{"11110101"});
    EXPECT_EQ(nsc::toHex(255), std::string{"FF"});
    EXPECT_EQ(nsc::toDecimal(245), std::string{"245"});
    EXPECT_EQ(nsc::groupBits(0xAC), std::string{"1010 1100"});
}

void converterTests() {
    nsc::Converter c;
    EXPECT_EQ(c.update("FF", nsc::Converter::Base::Hex), true);
    EXPECT_EQ(c.value(), std::uint64_t{255});
    EXPECT_EQ(c.as(nsc::Converter::Base::Decimal), std::string{"255"});
    EXPECT_EQ(c.as(nsc::Converter::Base::Binary), std::string{"11111111"});
    // A failed update must leave the previous value untouched.
    EXPECT_EQ(c.update("nope", nsc::Converter::Base::Decimal), false);
    EXPECT_EQ(c.value(), std::uint64_t{255});
}

}  // namespace

int main() {
    parseTests();
    formatTests();
    converterTests();
    if (g_failures == 0) {
        std::printf("All tests passed.\n");
        return 0;
    }
    std::printf("%d test(s) failed.\n", g_failures);
    return 1;
}
