// ELF loader tests — parse_elf and load_elf_into_processor.
//
// Uses hand-crafted minimal MIPS ELF32 (LE) binaries so the tests have zero
// external dependencies (no cross-compiler required).  The ELF images are
// synthesised in-memory and fed through a std::istringstream.

#include "mips/elf_loader.h"
#include "mips/single_cycle_cpu.h"

#include <cassert>
#include <cstddef>
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

// ─── Byte patching ───────────────────────────────────────────────────────────
// The hardening tests build a valid image with make_elf and then corrupt one
// field, so each case isolates exactly one validation rule.

static void patch_u16(std::string& bytes, size_t offset, uint16_t value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

static void patch_u32(std::string& bytes, size_t offset, uint32_t value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

// Field offsets inside the single program header make_elf emits.
static constexpr size_t kPhdrOffset   = sizeof(Elf32Ehdr);
static constexpr size_t kPOffsetField = kPhdrOffset + 4;
static constexpr size_t kPVaddrField  = kPhdrOffset + 8;
static constexpr size_t kPFileszField = kPhdrOffset + 16;
static constexpr size_t kPMemszField  = kPhdrOffset + 20;

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

    patch_u32(bytes, kPMemszField, 8);  // 4 file bytes + 4 BSS bytes

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

// ─── Hardening: header and segment validation (#127) ─────────────────────────

static void test_reject_bad_phentsize() {
    // Entries are read as sizeof(Elf32Phdr) bytes but walked at e_phentsize
    // stride; a mismatch used to misparse silently.
    auto bytes = make_elf({0u}, 0, 0);
    patch_u16(bytes, offsetof(Elf32Ehdr, e_phentsize), 24);
    const auto img = parse_from_bytes(bytes);
    CHECK(!img.ok());
}

static void test_reject_phnum_past_eof() {
    // A header claiming far more program headers than the file can hold used
    // to drive a long seek/read loop; the table is now bounded by file size.
    auto bytes = make_elf({0u}, 0, 0);
    patch_u16(bytes, offsetof(Elf32Ehdr, e_phnum), 0xFFFF);
    const auto img = parse_from_bytes(bytes);
    CHECK(!img.ok());
}

static void test_reject_filesz_past_eof() {
    // p_filesz drove seg.data.resize() unchecked — a 4 GiB request from a
    // ~100-byte file. Now rejected before the allocation.
    auto bytes = make_elf({0u}, 0, 0);
    patch_u32(bytes, kPFileszField, 0xFFFF'0000u);
    patch_u32(bytes, kPMemszField, 0xFFFF'0000u);
    const auto img = parse_from_bytes(bytes);
    CHECK(!img.ok());
}

static void test_reject_memsz_below_filesz() {
    // ElfSegment documents memsz >= filesz; the BSS loop assumes it.
    auto bytes = make_elf({0u, 0u}, 0, 0);
    patch_u32(bytes, kPMemszField, 4);  // filesz is 8
    const auto img = parse_from_bytes(bytes);
    CHECK(!img.ok());
}

static void test_reject_segment_wrapping_address_space() {
    // vaddr + off is uint32_t arithmetic in the loader: a wrapping segment
    // would pass Memory's bounds check and overwrite low memory.
    auto bytes = make_elf({0u}, 0, 0);
    patch_u32(bytes, kPVaddrField, 0xFFFF'FFFCu);
    patch_u32(bytes, kPFileszField, 4);
    patch_u32(bytes, kPMemszField, 16);  // 0xFFFFFFFC + 16 wraps
    const auto img = parse_from_bytes(bytes);
    CHECK(!img.ok());
}

static void test_reject_offset_past_eof() {
    auto bytes = make_elf({0u}, 0, 0);
    patch_u32(bytes, kPOffsetField, 0xFFFF'FF00u);
    const auto img = parse_from_bytes(bytes);
    CHECK(!img.ok());
}

static void test_bss_only_segment_is_mapped() {
    // p_filesz == 0 with p_memsz > 0 is a pure .bss.  The loader used to skip
    // the whole segment, so the region was never zeroed.
    auto bytes = make_elf({0xAAAA'AAAAu}, 0x300, 0x300);
    patch_u32(bytes, kPFileszField, 0);
    patch_u32(bytes, kPMemszField, 16);

    const auto img = parse_from_bytes(bytes);
    CHECK(img.ok());
    CHECK(img.segments.size() == 1);
    CHECK(img.segments[0].filesz == 0);
    CHECK(img.segments[0].memsz == 16);
    CHECK(img.segments[0].data.empty());

    mips::SingleCycleCpu cpu(1u << 16);
    std::string          err;
    // Dirty the region first so the zero-fill is observable.
    for (uint32_t off = 0; off < 16; ++off)
        CHECK(cpu.mem().write_byte(0x300 + off, 0xCC));
    CHECK(mips::load_elf_into_processor(cpu, img, err));
    CHECK(cpu.mem().read_byte(0x300).value_or(0xFF) == 0);
    CHECK(cpu.mem().read_byte(0x30F).value_or(0xFF) == 0);
}

static void test_bss_past_end_of_memory_reports_error() {
    // The BSS loop discarded write_byte's result, so a memsz running off the
    // end of memory truncated silently instead of reporting it.
    auto bytes = make_elf({0xAAAA'AAAAu}, 0xFFFC, 0xFFFC);
    patch_u32(bytes, kPMemszField, 0x100);  // runs past the 64 KB boundary

    const auto img = parse_from_bytes(bytes);
    CHECK(img.ok());

    mips::SingleCycleCpu cpu(1u << 16);  // 64 KB
    std::string          err;
    CHECK(!mips::load_elf_into_processor(cpu, img, err));
    CHECK(!err.empty());
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
    test_reject_bad_phentsize();
    test_reject_phnum_past_eof();
    test_reject_filesz_past_eof();
    test_reject_memsz_below_filesz();
    test_reject_segment_wrapping_address_space();
    test_reject_offset_past_eof();
    test_bss_only_segment_is_mapped();
    test_bss_past_end_of_memory_reports_error();

    std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
