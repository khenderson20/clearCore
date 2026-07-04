#pragma once

// ─── mips/memory.h ───────────────────────────────────────────────────────────
// Memory is ISA-agnostic and now lives in <isa/memory.h>. This header re-exports
// it into the `mips` namespace so existing callers (`mips::Memory`) keep working
// unchanged. New backends should include <isa/memory.h> and use `isa::Memory`.

#include "isa/memory.h"

namespace mips {
using isa::Memory;
}  // namespace mips
