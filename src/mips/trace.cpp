#include "mips/trace.h"

#include <cstdlib>
#include <memory>
#include <string_view>
#include <utility>

#include <spdlog/sinks/stdout_color_sinks.h>

namespace mips {

namespace {

// Read the desired level from CLEARCORE_LOG_LEVEL, defaulting to `warn`.
// spdlog::level::from_str() returns `off` for anything it doesn't recognise, so
// a typo would silently disable logging entirely; guard against that by only
// honouring `off` when the text literally says "off".
spdlog::level::level_enum level_from_env() {
    // MSVC deprecates std::getenv in favour of its non-portable _dupenv_s.  The
    // standard function is not deprecated, the value is only read (never held
    // across an environment mutation), and this runs once from a function-local
    // static, so the suppression is scoped to this one call rather than being
    // turned off for the translation unit.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    const char* env = std::getenv("CLEARCORE_LOG_LEVEL");
#ifdef _MSC_VER
#pragma warning(pop)
#endif
    if (env == nullptr || *env == '\0') return spdlog::level::warn;

    const auto level = spdlog::level::from_str(env);
    if (level == spdlog::level::off && std::string_view(env) != "off") return spdlog::level::warn;
    return level;
}

std::shared_ptr<spdlog::logger> make_logger() {
    auto sink   = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>("clearcore", std::move(sink));
    logger->set_level(level_from_env());
    logger->set_pattern("[clearcore %^%L%$] %v");  // e.g. "[clearcore T] fetch pc=..."
    return logger;
}

}  // namespace

spdlog::logger& trace_log() {
    // Function-local static: constructed once, on first use, thread-safely.
    static const std::shared_ptr<spdlog::logger> logger = make_logger();
    return *logger;
}

}  // namespace mips
