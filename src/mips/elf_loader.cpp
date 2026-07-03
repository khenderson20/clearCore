#include "mips/elf_loader.h"

#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

namespace mips {

// ─── ELF32 on-disk structures ─────────────────────────────────────────────────
// Defined inline to avoid a dependency on <elf.h> (not available on all hosts).

static constexpr uint8_t  kElfMag0     = 0x7f;
static constexpr uint8_t  kElfMag1     = 'E';
static constexpr uint8_t  kElfMag2     = 'L';
static constexpr uint8_t  kElfMag3     = 'F';
static constexpr uint8_t  kElfClass32  = 1;  // ELFCLASS32
static constexpr uint8_t  kElfData2LSB = 1;  // little-endian
static constexpr uint16_t kEmMips      = 8;  // EM_MIPS
static constexpr uint16_t kEtExec      = 2;  // ET_EXEC
static constexpr uint16_t kEtRel       = 1;  // ET_REL
static constexpr uint32_t kPtLoad      = 1;  // PT_LOAD

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
static_assert(sizeof(Elf32Ehdr) == 52);

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
static_assert(sizeof(Elf32Phdr) == 32);
#pragma pack(pop)

// ─── Helper: read a fixed-size struct from stream ────────────────────────────
template <typename T> static bool stream_read(std::istream& in, T& out) {
    return static_cast<bool>(in.read(reinterpret_cast<char*>(&out), sizeof(T)));
}

// ─── parse_elf ────────────────────────────────────────────────────────────────
ElfImage parse_elf(std::istream& in) {
    ElfImage img;

    Elf32Ehdr ehdr{};
    if (!stream_read(in, ehdr)) {
        img.error = "failed to read ELF header (file too short)";
        return img;
    }

    // Magic number check.
    if (ehdr.e_ident[0] != kElfMag0 || ehdr.e_ident[1] != kElfMag1 || ehdr.e_ident[2] != kElfMag2 ||
        ehdr.e_ident[3] != kElfMag3) {
        img.error = "not an ELF file (bad magic)";
        return img;
    }

    // 32-bit only.
    if (ehdr.e_ident[4] != kElfClass32) {
        img.error = "only ELF32 is supported (got ELF64)";
        return img;
    }

    // Little-endian (mipsel) only.  Big-endian MIPS ELF would require
    // byte-swapping instruction words but not byte arrays — we can't know
    // which is which at segment granularity.
    if (ehdr.e_ident[5] != kElfData2LSB) {
        img.error = "only little-endian MIPS ELF (mipsel) is supported; "
                    "recompile with mipsel-linux-gnu-gcc or mipsel-linux-musl-gcc";
        return img;
    }

    if (ehdr.e_machine != kEmMips) {
        img.error =
            "ELF machine type is not MIPS (e_machine=" + std::to_string(ehdr.e_machine) + ")";
        return img;
    }

    if (ehdr.e_type != kEtExec && ehdr.e_type != kEtRel) {
        img.error = "only ET_EXEC and ET_REL ELF types are supported";
        return img;
    }

    if (ehdr.e_phnum == 0 || ehdr.e_phoff == 0) {
        img.error = "ELF has no program headers — is this a relocatable object "
                    "without a linker script?  Use -static -Ttext=0x0 or link "
                    "with a MEMORY script.";
        return img;
    }

    img.entry = ehdr.e_entry;

    // Read PT_LOAD segments.
    for (uint16_t i = 0; i < ehdr.e_phnum; ++i) {
        const auto phoff =
            static_cast<std::streamoff>(ehdr.e_phoff) +
            static_cast<std::streamoff>(i) * static_cast<std::streamoff>(ehdr.e_phentsize);
        in.seekg(phoff);
        if (!in) {
            img.error = "failed to seek to program header " + std::to_string(i);
            return img;
        }

        Elf32Phdr phdr{};
        if (!stream_read(in, phdr)) {
            img.error = "failed to read program header " + std::to_string(i);
            return img;
        }

        if (phdr.p_type != kPtLoad) continue;
        if (phdr.p_filesz == 0) continue;

        // Read the raw segment data from the file.
        in.seekg(static_cast<std::streamoff>(phdr.p_offset));
        if (!in) {
            img.error = "failed to seek to segment " + std::to_string(i) + " data (offset 0x" +
                        [&] {
                            std::ostringstream s;
                            s << std::hex << phdr.p_offset;
                            return s.str();
                        }() +
                        ")";
            return img;
        }

        ElfSegment seg;
        seg.vaddr  = phdr.p_vaddr;
        seg.filesz = phdr.p_filesz;
        seg.memsz  = phdr.p_memsz;
        seg.data.resize(phdr.p_filesz);
        if (!in.read(reinterpret_cast<char*>(seg.data.data()),
                     static_cast<std::streamsize>(phdr.p_filesz))) {
            img.error = "failed to read segment " + std::to_string(i) + " data";
            return img;
        }

        img.segments.push_back(std::move(seg));
    }

    if (img.segments.empty()) {
        img.error = "ELF has no PT_LOAD segments — nothing to load";
    }

    return img;
}

// ─── load_elf_file ────────────────────────────────────────────────────────────
ElfImage load_elf_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        ElfImage img;
        img.error = "cannot open '" + path + "'";
        return img;
    }
    return parse_elf(f);
}

// ─── load_elf_into_processor ─────────────────────────────────────────────────
bool load_elf_into_processor(IProcessor& cpu, const ElfImage& image, std::string& error_out) {
    if (!image) {
        error_out = image.error.value_or("unknown ELF error");
        return false;
    }

    for (const ElfSegment& seg : image.segments) {
        // Write the file-content bytes directly into memory.
        for (uint32_t off = 0; off < seg.filesz; ++off) {
            if (!cpu.mem().write_byte(seg.vaddr + off, seg.data[off])) {
                std::ostringstream s;
                s << std::hex;
                s << "segment at 0x" << seg.vaddr << " extends outside the "
                  << "processor's address space (0x" << cpu.mem().size()
                  << " bytes); increase mem_bytes in the IProcessor constructor";
                error_out = s.str();
                return false;
            }
        }
        // Zero-fill the BSS portion (memsz > filesz).
        for (uint32_t off = seg.filesz; off < seg.memsz; ++off) {
            cpu.mem().write_byte(seg.vaddr + off, 0);
        }
    }

    cpu.set_pc(image.entry);
    return true;
}

// ─── load_elf_file_into_processor ────────────────────────────────────────────
bool load_elf_file_into_processor(IProcessor& cpu, const std::string& path,
                                  std::string& error_out) {
    const ElfImage img = load_elf_file(path);
    return load_elf_into_processor(cpu, img, error_out);
}

}  // namespace mips
