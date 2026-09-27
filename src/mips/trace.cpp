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
    const char* env = std::getenv("CLEARCORE_LOG_LEVEL");
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
