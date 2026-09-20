// libFuzzer harness for the ELF32 loader.
//
// parse_elf is the only parser in mips_core that sizes allocations and indexes
// the input with values taken straight from that input, which makes it the
// higher-value of the two fuzz targets.  It returns an ElfImage by value and
// touches no static state, so nothing needs resetting between iterations.

#include "mips/elf_loader.h"

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    std::istringstream in(std::string(reinterpret_cast<const char*>(data), size));
    const auto         img = mips::parse_elf(in);
    (void)img;
    return 0;
}
