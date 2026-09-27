// ─── golden_runner ────────────────────────────────────────────────────────────
// Headless CLI used by the MARS golden tests (tests/golden/run_golden.py).
//
// Loads a MARS "HexText" dump (one hex word per line — exactly the
// parse_hex_program format), executes it on the requested CPU model, and
// prints every register as "$name\t0xXXXXXXXX", matching the output of
// MARS's command-line register display so the two can be diffed directly.
//
// Memory layout mirrors MARS's CompactTextAtZero configuration:
//   .text at 0x0000, .data at 0x2000 — both fit the default 64 KiB Memory.
//
// Usage: golden_runner <single|pipelined> <text.hex> [data.hex]

#include "mips/pipelined_cpu.h"
#include "mips/processor.h"
#include "mips/program_loader.h"
#include "mips/registers.h"
#include "mips/single_cycle_cpu.h"

#include <cstdio>
#include <memory>
#include <string>

namespace {

constexpr uint32_t    kDataBase = 0x2000;  // MARS CompactTextAtZero .data base
constexpr std::size_t kMaxSteps = 100'000;

int fail(const std::string& msg) {
    std::fprintf(stderr, "golden_runner: %s\n", msg.c_str());
    return 1;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 3 || argc > 4)
        return fail("usage: golden_runner <single|pipelined> <text.hex> [data.hex]");

    const std::string                 model = argv[1];
    std::unique_ptr<mips::IProcessor> cpu;
    if (model == "single")
        cpu = std::make_unique<mips::SingleCycleCpu>();
    else if (model == "pipelined")
        cpu = std::make_unique<mips::PipelinedCpu>();
    else
        return fail("unknown CPU model '" + model + "'");

    const mips::HexProgram text = mips::load_hex_file(argv[2]);
    if (!text) return fail("text: " + *text.error);
    if (!cpu->load_program(text.words)) return fail("text segment does not fit in memory");

    if (argc == 4) {
        const mips::HexProgram data = mips::load_hex_file(argv[3]);
        if (!data) return fail("data: " + *data.error);
        if (!cpu->mem().load_words(kDataBase, data.words))
            return fail("data segment does not fit in memory");
    }

    // Halt (self-jump) is the normal exit; Ok means the step budget ran out,
    // which is also how MARS-side runs end (they hit the step limit spinning
    // on the same self-jump), so the register state is still comparable.
    if (cpu->run(kMaxSteps) == mips::StepResult::Fault)
        return fail("CPU faulted at pc=0x" + std::to_string(cpu->pc()));

    for (uint8_t i = 0; i < mips::RegisterFile::kCount; ++i)
        std::printf("$%s\t0x%08x\n", std::string(mips::register_abi_name(i)).c_str(),
                    cpu->regs().read(i));
    return 0;
}
