#pragma once

#include <ark/debugger.h>
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

#if ARKOSE_DEBUG
constexpr LogLevel CurrentLogLevel = LogLevel::Verbose;
#elif ARKOSE_DEVELOP || ARKOSE_RELEASE
constexpr LogLevel CurrentLogLevel = LogLevel::Info;
#endif

constexpr int FatalErrorExitCode = 13;

// See https://fmt.dev/latest/api.html#argument-lists for reference

template<LogLevel level>
inline void _internal_vlog(fmt::string_view format, fmt::format_args args)
{
    static_assert(level > LogLevel::None && level < LogLevel::All, "Invalid log level passed to log function");

    if constexpr (level <= CurrentLogLevel) {
        // TODO: Maybe add some more logging context (e.g. function name)?
        fmt::vprint(format, args);
        fmt::print("\n");
    }
}

template<LogLevel level, typename... Args>
[[noreturn]] inline std::enable_if_t<level == LogLevel::Fatal> _internal_log(fmt::format_string<Args...> format, Args&&... args)
{
    _internal_vlog<level>(format, fmt::make_format_args(args...));

    ARK_DEBUG_BREAK();
    exit(Logging::FatalErrorExitCode);
}

template<LogLevel level, typename... Args>
inline std::enable_if_t<level != LogLevel::Fatal> _internal_log(fmt::format_string<Args...> format, Args&&... args)
{
    _internal_vlog<level>(format, fmt::make_format_args(args...));
}

} // namespace Logging

#define MAKE_QUALIFIED_LOG_LEVEL(level) Logging::LogLevel::level

#define ARKOSE_LOG(logLevel, format, ...) Logging::_internal_log<MAKE_QUALIFIED_LOG_LEVEL(logLevel)>(FMT_STRING(format) __VA_OPT__(,) __VA_ARGS__)
