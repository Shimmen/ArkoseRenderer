#pragma once

#include "core/Debugger.h"
#include <fmt/format.h>
#include <cstdlib> // for exit()

namespace Logging {

enum class LogLevel {
    None = 0,
    Fatal,
    Error,
    Warning,
    Info,
    Verbose,
    All
};

constexpr LogLevel currentLogLevel = LogLevel::Info;
constexpr int errorAndExitExitCode = 13;

// See https://fmt.dev/latest/api.html#argument-lists for reference

template<LogLevel level>
inline void _internal_vlog(fmt::string_view format, fmt::format_args args)
{
    static_assert(level > LogLevel::None && level < LogLevel::All, "Invalid log level passed to log function");

    if constexpr (level <= currentLogLevel) {
        // TODO: Maybe add some more logging context (e.g. function name)?
        fmt::vprint(format, args);
        fmt::print("\n");
    }

    if constexpr (level == LogLevel::Fatal) {
        DEBUG_BREAK();
        exit(errorAndExitExitCode);
    }
}

template<LogLevel level, typename StringType, typename... Args>
inline void _internal_log(const StringType& format, Args&&... args)
{
    _internal_vlog<level>(format, fmt::make_args_checked<Args...>(format, args...));
}

} // namespace Logging

#define MAKE_QUALIFIED_LOG_LEVEL(level) Logging::LogLevel::##level

#define ARKOSE_LOG(logLevel, format, ...) Logging::_internal_log<MAKE_QUALIFIED_LOG_LEVEL(logLevel)>(FMT_STRING(format), __VA_ARGS__)