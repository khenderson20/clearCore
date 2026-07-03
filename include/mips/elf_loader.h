#pragma once

// ─── elf_loader.h ────────────────────────────────────────────────────────────
// Loads MIPS ELF32 executables into an IProcessor's memory and sets the
// initial program counter from the ELF entry point.
//
// Supported format:
//   • ELFCLASS32 (32-bit), ELFDATA2LSB (little-endian / mipsel)
//   • Static executables and relocatable objects (ET_EXEC, ET_REL)
//   • PT_LOAD program-header segments — the same mapping used by the OS loader
//
// How to build a compatible binary:
//   mipsel-linux-gnu-gcc -static -nostdlib -o hello hello.s
//   # or with the musl toolchain for full libc support:
//   mipsel-linux-musl-gcc -static -o hello hello.c
//
// Big-endian MIPS ELF files (ELFDATA2MSB) are rejected with a clear error
// because the emulator's Memory model is little-endian — byte-swapping would
// corrupt data segments.  Use a mipsel (little-endian) toolchain instead.

#include "mips/processor.h"

#include <cstdint>
#include <istream>
#include <optional>
#include <string>
#include <vector>

namespace mips {

// A PT_LOAD segment mapped from the ELF file.
struct ElfSegment {
    uint32_t             vaddr;   // virtual address to load at
    uint32_t             filesz;  // bytes from the file
    uint32_t             memsz;   // bytes in memory (memsz >= filesz; gap is zero-filled)
    std::vector<uint8_t> data;    // raw file content (length == filesz)
};

// Result of parsing an ELF file.
struct ElfImage {
    std::vector<ElfSegment>    segments;   // PT_LOAD segments, in order
    uint32_t                   entry = 0;  // initial PC (ELF entry point)
    std::optional<std::string> error;      // set on failure

    [[nodiscard]] bool ok() const noexcept { return !error.has_value(); }
    explicit           operator bool() const noexcept { return ok(); }
};

// Parse an ELF image from an arbitrary input stream.
[[nodiscard]] ElfImage parse_elf(std::istream& in);

// Open `path` and parse it as an ELF file.
[[nodiscard]] ElfImage load_elf_file(const std::string& path);

// Map all PT_LOAD segments of `image` into `cpu`'s memory and set the PC.
// Returns false and sets `error_out` if any segment falls outside the
// processor's address space.
bool load_elf_into_processor(IProcessor& cpu, const ElfImage& image, std::string& error_out);

// Convenience: open, parse, and load in one call.
bool load_elf_file_into_processor(IProcessor& cpu, const std::string& path, std::string& error_out);

}  // namespace mips
