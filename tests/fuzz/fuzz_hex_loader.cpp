#include "mips/program_loader.h"

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    std::istringstream in(std::string(reinterpret_cast<const char*>(data), size));
    auto               result = mips::parse_hex_program(in);
    (void)result;
    return 0;
}
