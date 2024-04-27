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

    // NOTE: If the noreturn behaviour is required to silence the compiler use ARKOSE_LOG_FATAL
    if (level == LogLevel::Fatal) {
        ARK_DEBUG_BREAK();
        exit(Logging::FatalErrorExitCode);
    }
}

template<LogLevel level, typename... Args>
inline void _internal_log(fmt::format_string<Args...> format, Args&&... args)
{
    _internal_vlog<level>(format, fmt::make_format_args(args...));
}

} // namespace Logging

#define MAKE_QUALIFIED_LOG_LEVEL(level) Logging::LogLevel::level

#if defined(_MSC_VER)
// It seems like MSVC may not yet support __VA_OPT__(..) fully... need to test this on MSVC for real, but this old version of my macro should work.
#define ARKOSE_LOG(logLevel, format, ...) Logging::_internal_log<MAKE_QUALIFIED_LOG_LEVEL(logLevel)>(FMT_STRING(format), __VA_ARGS__)
#define ARKOSE_LOG_FATAL(format, ...)  do { Logging::_internal_log<Logging::LogLevel::Fatal>(FMT_STRING(format), __VA_ARGS__); \
                                            exit(Logging::FatalErrorExitCode); /* ensure noreturn behaviour */ } while (false)
#else
#define ARKOSE_LOG(logLevel, format, ...) Logging::_internal_log<MAKE_QUALIFIED_LOG_LEVEL(logLevel)>(FMT_STRING(format) __VA_OPT__(,) __VA_ARGS__)
#define ARKOSE_LOG_FATAL(format, ...)  do { Logging::_internal_log<Logging::LogLevel::Fatal>(FMT_STRING(format) __VA_OPT__(,) __VA_ARGS__); \
                                            exit(Logging::FatalErrorExitCode); /* ensure noreturn behaviour */ } while (false)
#endif
