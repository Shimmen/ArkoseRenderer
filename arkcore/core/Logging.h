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

constexpr LogLevel CurrentLogLevel = LogLevel::Info;
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

    // NOTE: If the noreturn behaviour is required to silence the compiler use ARKOSE_LOG_FATAL
    if (level == LogLevel::Fatal) {
        ARK_DEBUG_BREAK();
        exit(Logging::FatalErrorExitCode);
    }
}

template<LogLevel level, typename StringType, typename... Args>
inline void _internal_log(const StringType& format, Args&&... args)
{
    _internal_vlog<level>(format, fmt::make_args_checked<Args...>(format, args...));
}

} // namespace Logging

#define MAKE_QUALIFIED_LOG_LEVEL(level) Logging::LogLevel::level

#if defined(__clang__)
// TODO: Fix logging macros for clang!
#define ARKOSE_LOG(logLevel, format, ...)
#define ARKOSE_LOG_FATAL(format, ...)
#else
#define ARKOSE_LOG(logLevel, format, ...) Logging::_internal_log<MAKE_QUALIFIED_LOG_LEVEL(logLevel)>(FMT_STRING(format), __VA_ARGS__)
#define ARKOSE_LOG_FATAL(format, ...)  do { Logging::_internal_log<Logging::LogLevel::Fatal>(FMT_STRING(format), __VA_ARGS__); \
                                            exit(Logging::FatalErrorExitCode); /* ensure noreturn behaviour */ } while (false)
#endif
