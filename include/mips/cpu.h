#pragma once

// ─── cpu.h — backward-compatibility shim ─────────────────────────────────────
// All pre-refactor code that includes <mips/cpu.h> and uses `mips::Cpu` continues
// to compile unchanged.  New code should include <mips/single_cycle_cpu.h> or
// <mips/pipelined_cpu.h> directly, and program to the IProcessor interface.

#include "mips/pipelined_cpu.h"
#include "mips/processor.h"
#include "mips/single_cycle_cpu.h"

namespace mips {

// `Cpu` is an alias for the single-cycle implementation — identical semantics
// to the original class, just now deriving from IProcessor.
using Cpu = SingleCycleCpu;

}  // namespace mips
