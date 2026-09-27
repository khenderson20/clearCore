#pragma once

// ─── trace.h ─────────────────────────────────────────────────────────────────
// Central spdlog logger for the MIPS core: instruction tracing, pipeline
// hazard/stall/flush events, exception raises, and memory faults.
//
// The logger is created lazily on first use and defaults to the `warn` level so
// normal runs, unit tests, and both GUIs stay quiet. Opt in at runtime via the
// CLEARCORE_LOG_LEVEL environment variable, e.g.
//
//     CLEARCORE_LOG_LEVEL=trace ./number_system_converter
//
// Accepted values are spdlog's level names: trace, debug, info, warn, error,
// critical, off. Output goes to stderr so stdout stays clean for program output.
//
// Hot-path callers should guard disassembly/formatting work behind
// mips::trace_enabled(level) so a quiet run pays nothing beyond a level compare.

#include <spdlog/spdlog.h>

namespace mips {

// The shared "clearcore" logger. Thread-safe; safe to call from anywhere.
[[nodiscard]] spdlog::logger& trace_log();

// True when a message at `level` would actually be emitted. Use this to skip
// building trace strings (e.g. disassembly) on quiet runs.
[[nodiscard]] inline bool trace_enabled(spdlog::level::level_enum level) {
    return trace_log().should_log(level);
}

}  // namespace mips
