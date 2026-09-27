// ELF loader tests — parse_elf and load_elf_into_processor.
//
// Uses hand-crafted minimal MIPS ELF32 (LE) binaries so the tests have zero
// external dependencies (no cross-compiler required).  The ELF images are
// synthesised in-memory and fed through a std::istringstream.

#include "mips/elf_loader.h"
#include "mips/single_cycle_cpu.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
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

// ─── Minimal ELF32 builder ────────────────────────────────────────────────────
// Constructs the smallest valid MIPS LE ELF32 executable in memory.

#pragma pack(push, 1)
struct Elf32Ehdr {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};
struct Elf32Phdr {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
};
#pragma pack(pop)

// Build a one-segment ELF: `words` loaded at `vaddr`, entry = `entry`.
static std::string make_elf(const std::vector<uint32_t>& words, uint32_t vaddr, uint32_t entry) {
    const auto seg_bytes = static_cast<uint32_t>(words.size() * sizeof(uint32_t));
    // File layout: ELF header, then one program header, then segment data.
    const uint32_t phdr_offset = sizeof(Elf32Ehdr);
    const uint32_t data_offset = phdr_offset + sizeof(Elf32Phdr);

    Elf32Ehdr eh{};
    eh.e_ident[0]  = 0x7f;
    eh.e_ident[1]  = 'E';
    eh.e_ident[2]  = 'L';
    eh.e_ident[3]  = 'F';
    eh.e_ident[4]  = 1;  // ELFCLASS32
    eh.e_ident[5]  = 1;  // ELFDATA2LSB
    eh.e_ident[6]  = 1;  // EV_CURRENT
    eh.e_type      = 2;  // ET_EXEC
    eh.e_machine   = 8;  // EM_MIPS
    eh.e_version   = 1;
    eh.e_entry     = entry;
    eh.e_phoff     = phdr_offset;
    eh.e_ehsize    = sizeof(Elf32Ehdr);
    eh.e_phentsize = sizeof(Elf32Phdr);
    eh.e_phnum     = 1;

    Elf32Phdr ph{};
    ph.p_type   = 1;  // PT_LOAD
    ph.p_offset = data_offset;
    ph.p_vaddr  = vaddr;
    ph.p_paddr  = vaddr;
    ph.p_filesz = seg_bytes;
    ph.p_memsz  = seg_bytes;
    ph.p_flags  = 5;  // PF_R | PF_X
    ph.p_align  = 4;

    std::string out;
    out.resize(data_offset + seg_bytes);
    std::memcpy(out.data(), &eh, sizeof(eh));
    std::memcpy(out.data() + phdr_offset, &ph, sizeof(ph));
    std::memcpy(out.data() + data_offset, words.data(), seg_bytes);
    return out;
}

static mips::ElfImage parse_from_bytes(const std::string& bytes) {
    std::istringstream s(bytes);
    return mips::parse_elf(s);
}

// ─── Tests ───────────────────────────────────────────────────────────────────

static void test_parse_valid_elf() {
    // Two-word segment at vaddr=0, entry=0.
    const std::vector<uint32_t> words = {0x00000000u, 0x0800'0000u};
    const auto                  img   = parse_from_bytes(make_elf(words, 0, 0));
    CHECK(img.ok());
    CHECK(img.entry == 0);
    CHECK(img.segments.size() == 1);
    CHECK(img.segments[0].vaddr == 0);
    CHECK(img.segments[0].filesz == 8);
    CHECK(img.segments[0].data.size() == 8);
    // Verify the raw bytes in the segment.
    CHECK(img.segments[0].data[0] == 0x00);
    CHECK(img.segments[0].data[4] == 0x00);
    CHECK(img.segments[0].data[7] == 0x08);
}

static void test_parse_entry_point() {
    const std::vector<uint32_t> words = {0x0800'0001u};
    const auto                  img   = parse_from_bytes(make_elf(words, 0x1000, 0x1004));
    CHECK(img.ok());
    CHECK(img.entry == 0x1004u);
    CHECK(img.segments[0].vaddr == 0x1000u);
}

static void test_parse_bad_magic() {
    std::string bad(16 + 36, '\0');  // short header, wrong magic
    bad[0] = 'N';
    bad[1] = 'O';
    bad[2] = 'P';
    bad[3] = 'E';
    std::istringstream s(bad);
    const auto         img = mips::parse_elf(s);
    CHECK(!img.ok());
    CHECK(img.error.has_value());
}

static void test_parse_big_endian_rejected() {
    auto bytes = make_elf({0u}, 0, 0);
    bytes[5]   = 2;  // ELFDATA2MSB — patch endianness byte
    std::istringstream s(bytes);
    const auto         img = mips::parse_elf(s);
    CHECK(!img.ok());
}

static void test_parse_wrong_machine() {
    auto bytes = make_elf({0u}, 0, 0);
    // Patch e_machine to x86 (3) at offset 18 (LE).
    bytes[18] = 3;
    bytes[19] = 0;
    std::istringstream s(bytes);
    const auto         img = mips::parse_elf(s);
    CHECK(!img.ok());
}

static void test_parse_too_short() {
    const std::string  empty;
    std::istringstream s(empty);
    const auto         img = mips::parse_elf(s);
    CHECK(!img.ok());
}

static void test_load_into_processor() {
    // Halt instruction (J self at address 0): 0x08000000
    const std::vector<uint32_t> words = {0x0800'0000u};
    const auto                  img   = parse_from_bytes(make_elf(words, 0, 0));
    CHECK(img.ok());

    mips::SingleCycleCpu cpu(1u << 16);
    std::string          err;
    const bool           ok = mips::load_elf_into_processor(cpu, img, err);
    CHECK(ok);
    CHECK(cpu.pc() == 0u);

    // The loaded word at address 0 should be the halt instruction.
    const auto w = cpu.mem().read_word(0);
    CHECK(w.has_value());
    CHECK(*w == 0x0800'0000u);

    // Actually running one step should return Halt.
    const auto r = cpu.step();
    CHECK(r == mips::StepResult::Halt);
}

static void test_load_respects_vaddr() {
    // Load two words at vaddr=0x0100, entry=0x0100.
    const std::vector<uint32_t> words = {0xDEAD'BEEFu, 0xCAFE'BABEu};
    const auto                  img   = parse_from_bytes(make_elf(words, 0x0100, 0x0100));
    CHECK(img.ok());

    mips::SingleCycleCpu cpu(1u << 16);
    std::string          err;
    CHECK(mips::load_elf_into_processor(cpu, img, err));
    CHECK(cpu.pc() == 0x0100u);
    CHECK(cpu.mem().read_word(0x0100).value_or(0) == 0xDEAD'BEEFu);
    CHECK(cpu.mem().read_word(0x0104).value_or(0) == 0xCAFE'BABEu);
}

static void test_load_out_of_bounds() {
    // Segment at vaddr beyond the processor's 64KB memory.
    const std::vector<uint32_t> words = {0u};
    const auto                  img   = parse_from_bytes(make_elf(words, 0x1'0000, 0x1'0000));
    CHECK(img.ok());

    mips::SingleCycleCpu cpu(1u << 16);  // 64 KB
    std::string          err;
    const bool           ok = mips::load_elf_into_processor(cpu, img, err);
    CHECK(!ok);
    CHECK(!err.empty());
}

static void test_bss_zero_fill() {
    // Build a segment where memsz > filesz so BSS is zero-filled.
    // We patch the phdr by hand.
    const std::vector<uint32_t> words = {0xAAAA'AAAAu};
    auto                        bytes = make_elf(words, 0x200, 0x200);

    // Patch p_memsz at offset sizeof(Elf32Ehdr) + 24 (p_memsz is the 6th field).
    const size_t memsz_off = sizeof(Elf32Ehdr) + 20;  // p_memsz offset in Elf32Phdr
    uint32_t     new_memsz = 8;                       // 4 file bytes + 4 BSS bytes
    std::memcpy(bytes.data() + memsz_off, &new_memsz, 4);

    std::istringstream s(bytes);
    const auto         img = mips::parse_elf(s);
    CHECK(img.ok());
    CHECK(img.segments[0].memsz == 8);
    CHECK(img.segments[0].filesz == 4);

    mips::SingleCycleCpu cpu(1u << 16);
    std::string          err;
    CHECK(mips::load_elf_into_processor(cpu, img, err));
    // BSS byte at vaddr+4 should be zero.
    CHECK(cpu.mem().read_byte(0x204).value_or(0xFF) == 0);
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main() {
    test_parse_valid_elf();
    test_parse_entry_point();
    test_parse_bad_magic();
    test_parse_big_endian_rejected();
    test_parse_wrong_machine();
    test_parse_too_short();
    test_load_into_processor();
    test_load_respects_vaddr();
    test_load_out_of_bounds();
    test_bss_zero_fill();

    std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
