#include "mips/elf_loader.h"

#include <cstring>
#include <fstream>
#include <ios>
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

// One past the highest MIPS32 byte address.  Segment placement is checked
// against this because the loader walks `vaddr + off` in uint32_t arithmetic:
// a segment that wraps would silently overwrite low memory instead of being
// rejected by Memory's bounds check.
static constexpr uint64_t kAddressSpaceEnd = 0x1'0000'0000ULL;

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

// ─── Helper: total length of a seekable stream ───────────────────────────────
// Every offset and size taken from the file is validated against this, which
// is what keeps a crafted header from driving an allocation or a read past the
// real input.  Bounding on the true length also avoids depending on seekg()
// setting failbit past EOF — libstdc++ and MSVC disagree about that.
static bool stream_size(std::istream& in, uint64_t& out) {
    in.clear();
    in.seekg(0, std::ios::end);
    if (!in) return false;
    const std::streamoff end = in.tellg();
    if (end < 0) return false;
    out = static_cast<uint64_t>(end);
    return true;
}

// ─── Helper: format a value as bare hex for error messages ───────────────────
static std::string hex(uint32_t value) {
    std::ostringstream s;
    s << std::hex << value;
    return s.str();
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

    // Everything below indexes the file with values the file itself supplies,
    // so bound them against its real length first.
    uint64_t file_size = 0;
    if (!stream_size(in, file_size)) {
        img.error = "cannot determine the size of the ELF input (stream is not seekable)";
        return img;
    }

    // Each entry is read as sizeof(Elf32Phdr) bytes but the table is walked at
    // a stride of e_phentsize.  If the two disagree every entry after the first
    // is read from the wrong place, and the file misparses without any error.
    if (ehdr.e_phentsize != sizeof(Elf32Phdr)) {
        img.error = "unsupported program-header entry size (e_phentsize=" +
                    std::to_string(ehdr.e_phentsize) + ", expected " +
                    std::to_string(sizeof(Elf32Phdr)) + ")";
        return img;
    }

    // Bounding the whole table at once also caps e_phnum: a header claiming
    // 65535 entries cannot survive this unless the file really is that long.
    const uint64_t phdr_table_end =
        static_cast<uint64_t>(ehdr.e_phoff) +
        static_cast<uint64_t>(ehdr.e_phnum) * static_cast<uint64_t>(ehdr.e_phentsize);
    if (phdr_table_end > file_size) {
        img.error = "program-header table (" + std::to_string(ehdr.e_phnum) +
                    " entries at file offset 0x" + hex(ehdr.e_phoff) +
                    ") extends past the end of the file";
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

        // A segment with no file content and no memory image contributes
        // nothing.  One with memsz > filesz == 0 is a pure .bss and must still
        // be mapped so load_elf_into_processor zero-fills it.
        if (phdr.p_filesz == 0 && phdr.p_memsz == 0) continue;

        // ElfSegment documents memsz >= filesz, and the zero-fill loop in the
        // loader relies on it.
        if (phdr.p_memsz < phdr.p_filesz) {
            img.error = "segment " + std::to_string(i) + " has p_memsz (0x" + hex(phdr.p_memsz) +
                        ") smaller than p_filesz (0x" + hex(phdr.p_filesz) + ")";
            return img;
        }

        if (static_cast<uint64_t>(phdr.p_vaddr) + static_cast<uint64_t>(phdr.p_memsz) >
            kAddressSpaceEnd) {
            img.error = "segment " + std::to_string(i) + " at 0x" + hex(phdr.p_vaddr) + " (0x" +
                        hex(phdr.p_memsz) +
                        " bytes) wraps past the end of the 32-bit "
                        "address space";
            return img;
        }

        ElfSegment seg;
        seg.vaddr  = phdr.p_vaddr;
        seg.filesz = phdr.p_filesz;
        seg.memsz  = phdr.p_memsz;

        if (phdr.p_filesz > 0) {
            // Checked before the resize: p_filesz is a uint32_t straight from
            // the file, so an unbounded resize would let a 64-byte input ask
            // for 4 GiB.
            if (static_cast<uint64_t>(phdr.p_offset) + static_cast<uint64_t>(phdr.p_filesz) >
                file_size) {
                img.error = "segment " + std::to_string(i) + " data (0x" + hex(phdr.p_filesz) +
                            " bytes at file offset 0x" + hex(phdr.p_offset) +
                            ") extends past the end of the file";
                return img;
            }

            in.seekg(static_cast<std::streamoff>(phdr.p_offset));
            if (!in) {
                img.error = "failed to seek to segment " + std::to_string(i) + " data (offset 0x" +
                            hex(phdr.p_offset) + ")";
                return img;
            }

            seg.data.resize(phdr.p_filesz);
            if (!in.read(reinterpret_cast<char*>(seg.data.data()),
                         static_cast<std::streamsize>(phdr.p_filesz))) {
                img.error = "failed to read segment " + std::to_string(i) + " data";
                return img;
            }
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

    const auto out_of_range = [&cpu](const ElfSegment& seg) {
        std::ostringstream s;
        s << std::hex;
        s << "segment at 0x" << seg.vaddr << " extends outside the "
          << "processor's address space (0x" << cpu.mem().size()
          << " bytes); increase mem_bytes in the IProcessor constructor";
        return s.str();
    };

    for (const ElfSegment& seg : image.segments) {
        // Write the file-content bytes directly into memory.
        for (uint32_t off = 0; off < seg.filesz; ++off) {
            if (!cpu.mem().write_byte(seg.vaddr + off, seg.data[off])) {
                error_out = out_of_range(seg);
                return false;
            }
        }
        // Zero-fill the BSS portion (memsz > filesz).  Checked like the loop
        // above: a .bss running past the end of memory is the same error, and
        // ignoring the result would truncate it silently instead.
        for (uint32_t off = seg.filesz; off < seg.memsz; ++off) {
            if (!cpu.mem().write_byte(seg.vaddr + off, 0)) {
                error_out = out_of_range(seg);
                return false;
            }
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
