# ELF Loader

clearCore can now load real compiled MIPS binaries directly into the emulator's memory, instead of requiring hand-assembled word arrays or the custom hex format. The ELF loader parses MIPS ELF32 executables, maps each `PT_LOAD` segment into the processor's address space, and sets the initial PC to the ELF entry point.

## Supported format

| Property | Supported value |
|----------|----------------|
| Architecture | EM_MIPS (8) |
| Class | ELFCLASS32 (32-bit) |
| Endianness | **ELFDATA2LSB (little-endian / mipsel only)** |
| Type | ET_EXEC (static executable) |
| Segments | All `PT_LOAD` segments are mapped; BSS (memsz > filesz) is zero-filled |

Big-endian MIPS ELF files (`ELFDATA2MSB`) are rejected with a clear error message. The emulator's `Memory` model is little-endian, so loading a big-endian ELF without byte-swapping every instruction would produce garbage. Use a `mipsel` (little-endian) toolchain instead.

## Building compatible programs

### Bare-metal assembly (recommended for learning)

```bash
# Install the mipsel cross-toolchain
sudo dnf install gcc-mipsel-linux-gnu binutils-mipsel-linux-gnu  # Fedora
sudo apt install binutils-mipsel-linux-gnu gcc-mipsel-linux-gnu  # Debian/Ubuntu

# hello.s: add two numbers and store the result
.text
.global _start
_start:
    ori  $t0, $zero, 10
    ori  $t1, $zero, 20
    add  $t2, $t0, $t1      # t2 = 30
    # Halt: self-targeting jump
    b    .

# Assemble and link
mipsel-linux-gnu-as -mips32r2 -EL -o hello.o hello.s
mipsel-linux-gnu-ld -e _start -Ttext=0x0000 -o hello hello.o
```

### C programs with musl libc

```bash
# musl gives you a self-contained static libc without glibc baggage
# Install: https://musl.cc/ or build from source

mipsel-linux-musl-gcc -static -O2 -o hello hello.c
```

### Running from the assembler hex format (legacy)

The existing `load_hex_file()` / `parse_hex_program()` API still works and is unchanged.

## C++ API

```cpp
#include "mips/elf_loader.h"
#include "mips/single_cycle_cpu.h"

mips::SingleCycleCpu cpu(4u << 20);  // 4 MB address space
std::string err;

// One-shot: open, parse, and load in one call.
if (!mips::load_elf_file_into_processor(cpu, "hello", err)) {
    std::cerr << "ELF load failed: " << err << "\n";
    return 1;
}
// cpu.pc() is now the ELF entry point.
```

Or in two steps if you need to inspect the image first:

```cpp
mips::ElfImage img = mips::load_elf_file("hello");
if (!img) {
    std::cerr << img.error.value() << "\n";
    return 1;
}

std::cout << "Entry: 0x" << std::hex << img.entry << "\n";
for (const auto& seg : img.segments)
    std::cout << "  PT_LOAD vaddr=0x" << seg.vaddr
              << " size=" << seg.filesz << "\n";

std::string err;
mips::load_elf_into_processor(cpu, img, err);
```

Parsing from an in-memory stream:

```cpp
std::istringstream buf(elf_bytes);
mips::ElfImage img = mips::parse_elf(buf);
```

## Memory sizing

The default `IProcessor` constructor allocates 64 KB. Most real programs need more. Increase it when constructing the CPU:

```cpp
mips::SingleCycleCpu cpu(4u << 20);  // 4 MB
mips::PipelinedCpu   cpu(1u << 24);  // 16 MB
```

The ELF loader will return an error with a descriptive message if any segment does not fit.

## Impact

| Without ELF loader | With ELF loader |
|---|---|
| Programs must be hand-assembled as `std::vector<uint32_t>` or custom `.hex` files | Compile with `mipsel-linux-gnu-gcc` and load directly |
| Only the ISA subset implemented in the emulator can be exercised | Any valid MIPS LE binary can run (within the emulated ISA subset) |
| Testing requires writing instruction encoders by hand | Unit tests can use real compiled programs for regression coverage |
| Students cannot reuse existing MIPS assembly from coursework | Students can load standard MIPS assignments directly |

The ELF loader is also the prerequisite for the upcoming syscall emulation (Stage 5 stretch goal): once programs can set up a Linux O32 ABI stack and call `$v0=4` (`write`), the exception handler can intercept SYSCALL and forward it to the host OS.
