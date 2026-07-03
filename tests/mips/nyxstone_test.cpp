// nyxstone_test.cpp — differential validation of the hand-written Decoder +
// Disassembler against Nyxstone (LLVM's MC layer) for little-endian MIPS32.
//
// For each instruction word in the corpus we:
//   1. decode + disassemble it with clearCore's own Decoder/Disassembler,
//   2. re-assemble that text with Nyxstone (LLVM),
//   3. assert LLVM produced exactly one word equal to the original.
//
// A pass means our disassembly is semantically faithful: an independent
// production assembler re-encodes it to the identical bits. This is the raison
// d'être for the Nyxstone dependency.
//
// Requires CLEARCORE_NYXSTONE_ENABLED (an in-range LLVM found at configure
// time). When Nyxstone is disabled the test compiles to a no-op that passes.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "mips/decoder.h"
#include "mips/disassembler.h"

#if CLEARCORE_NYXSTONE_ENABLED
#include "mips/nyxstone_backend.h"
#endif

using namespace mips;

static int g_passed = 0, g_failed = 0;

// ── Encoding helpers (H&H Appendix B) ─────────────────────────────────────────
static uint32_t enc_r(uint8_t rs, uint8_t rt, uint8_t rd, uint8_t sh, uint8_t funct) {
    return (static_cast<uint32_t>(rs) << 21) | (static_cast<uint32_t>(rt) << 16) |
           (static_cast<uint32_t>(rd) << 11) | (static_cast<uint32_t>(sh) << 6) |
           static_cast<uint32_t>(funct);
}
static uint32_t enc_i(uint8_t op, uint8_t rs, uint8_t rt, uint16_t imm) {
    return (static_cast<uint32_t>(op) << 26) | (static_cast<uint32_t>(rs) << 21) |
           (static_cast<uint32_t>(rt) << 16) | static_cast<uint32_t>(imm);
}

#if CLEARCORE_NYXSTONE_ENABLED

// One differential case: our disassembly of `word` must re-assemble (via LLVM)
// back to `word`.
static void check_roundtrip(const NyxstoneBackend& nyx, uint32_t word, uint32_t pc = 0) {
    const auto decoded = Decoder::decode(word);
    if (!decoded) {
        ++g_failed;
        std::printf("  FAIL 0x%08x  our Decoder rejected a word LLVM should know\n", word);
        return;
    }
    const std::string our_text = Disassembler::to_string(*decoded, pc);

    const auto asmres = nyx.assemble(our_text, pc);
    if (!asmres) {
        ++g_failed;
        std::printf("  FAIL 0x%08x  \"%s\"  LLVM rejected our text: %s\n", word, our_text.c_str(),
                    asmres.error.c_str());
        return;
    }
    if (asmres.value.size() != 1) {
        ++g_failed;
        std::printf("  FAIL 0x%08x  \"%s\"  LLVM produced %zu words, expected 1\n", word,
                    our_text.c_str(), asmres.value.size());
        return;
    }
    if (asmres.value[0] != word) {
        ++g_failed;
        std::printf("  FAIL 0x%08x  \"%s\"  LLVM re-encoded to 0x%08x\n", word, our_text.c_str(),
                    asmres.value[0]);
        return;
    }
    ++g_passed;
}

static void run(const NyxstoneBackend& nyx) {
    // ── R-type arithmetic/logic (rs=$t0=8, rt=$t1=9, rd=$t2=10) ──
    check_roundtrip(nyx, enc_r(8, 9, 10, 0, 0x20));  // add  $t2,$t0,$t1
    check_roundtrip(nyx, enc_r(8, 9, 10, 0, 0x21));  // addu $t2,$t0,$t1
    check_roundtrip(nyx, enc_r(8, 9, 10, 0, 0x22));  // sub  $t2,$t0,$t1
    check_roundtrip(nyx, enc_r(8, 9, 10, 0, 0x23));  // subu $t2,$t0,$t1
    check_roundtrip(nyx, enc_r(8, 9, 10, 0, 0x24));  // and  $t2,$t0,$t1
    check_roundtrip(nyx, enc_r(8, 9, 10, 0, 0x25));  // or   $t2,$t0,$t1
    check_roundtrip(nyx, enc_r(8, 9, 10, 0, 0x26));  // xor  $t2,$t0,$t1
    check_roundtrip(nyx, enc_r(8, 9, 10, 0, 0x27));  // nor  $t2,$t0,$t1
    check_roundtrip(nyx, enc_r(8, 9, 10, 0, 0x2A));  // slt  $t2,$t0,$t1
    check_roundtrip(nyx, enc_r(8, 9, 10, 0, 0x2B));  // sltu $t2,$t0,$t1

    // ── R-type shifts (rt=$t1=9, rd=$t0=8, shamt=4; var-shift rs=$t2=10) ──
    check_roundtrip(nyx, enc_r(0, 9, 8, 4, 0x00));   // sll  $t0,$t1,4
    check_roundtrip(nyx, enc_r(0, 9, 8, 4, 0x02));   // srl  $t0,$t1,4
    check_roundtrip(nyx, enc_r(0, 9, 8, 4, 0x03));   // sra  $t0,$t1,4
    check_roundtrip(nyx, enc_r(10, 9, 8, 0, 0x04));  // sllv $t0,$t1,$t2
    check_roundtrip(nyx, enc_r(10, 9, 8, 0, 0x06));  // srlv $t0,$t1,$t2

    // ── jr $ra ──
    check_roundtrip(nyx, enc_r(31, 0, 0, 0, 0x08));  // jr $ra

    // ── I-type arithmetic/logic (rs=$t0=8, rt=$t1=9) ──
    check_roundtrip(nyx, enc_i(0x08, 8, 9, 100));   // addi  $t1,$t0,100
    check_roundtrip(nyx, enc_i(0x09, 8, 9, 100));   // addiu $t1,$t0,100
    check_roundtrip(nyx, enc_i(0x0A, 8, 9, 5));     // slti  $t1,$t0,5
    check_roundtrip(nyx, enc_i(0x0B, 8, 9, 5));     // sltiu $t1,$t0,5
    check_roundtrip(nyx, enc_i(0x0C, 8, 9, 0xFF));  // andi  $t1,$t0,0xff
    check_roundtrip(nyx, enc_i(0x0D, 8, 9, 0xFF));  // ori   $t1,$t0,0xff
    check_roundtrip(nyx, enc_i(0x0E, 8, 9, 0xFF));  // xori  $t1,$t0,0xff

    // ── lui $t1, 0x1234 ──
    check_roundtrip(nyx, enc_i(0x0F, 0, 9, 0x1234));

    // ── Loads / stores: rt=$t1=9, base rs=$sp=29, offset ±N ──
    check_roundtrip(nyx, enc_i(0x23, 29, 9, 8));                          // lw  $t1,8($sp)
    check_roundtrip(nyx, enc_i(0x2B, 29, 9, 8));                          // sw  $t1,8($sp)
    check_roundtrip(nyx, enc_i(0x24, 29, 9, 8));                          // lbu $t1,8($sp)
    check_roundtrip(nyx, enc_i(0x25, 29, 9, 8));                          // lhu $t1,8($sp)
    check_roundtrip(nyx, enc_i(0x23, 29, 9, static_cast<uint16_t>(-4)));  // lw  $t1,-4($sp)

    // ── Exercise the disassemble() path: LLVM's text must name the same
    //    mnemonic our Decoder does, for a couple of representative words. ──
    struct DisCase {
        uint32_t    word;
        const char* mnemonic;
    };
    for (const auto& c :
         {DisCase{enc_r(8, 9, 10, 0, 0x20), "add"}, DisCase{enc_i(0x23, 29, 9, 8), "lw"},
          DisCase{enc_r(31, 0, 0, 0, 0x08), "jr"}}) {
        const auto dis = nyx.disassemble({c.word});
        if (dis && dis.value.find(c.mnemonic) != std::string::npos) {
            ++g_passed;
        } else {
            ++g_failed;
            std::printf("  FAIL 0x%08x  disassemble did not yield \"%s\" (got: %s%s)\n", c.word,
                        c.mnemonic,
                        dis ? dis.value.c_str() : "<error: ", dis ? "" : dis.error.c_str());
        }
    }
}

int main() {
    auto backend = NyxstoneBackend::create();
    if (!backend) {
        std::printf("nyxstone_test: backend unavailable (LLVM Mips target missing) — skipping\n");
        return 0;
    }
    run(*backend);
    std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}

#else  // !CLEARCORE_NYXSTONE_ENABLED

int main() {
    std::printf(
        "nyxstone_test: built without Nyxstone (CLEARCORE_NYXSTONE_ENABLED=0) — skipping\n");
    return 0;
}

#endif
