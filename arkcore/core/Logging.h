#pragma once

#include <ark/debugger.h>
#include <fmt/color.h>
#include <fmt/format.h>
#include <fmt/std.h> // for fmtlib formatters for std types
#include <magic_enum/magic_enum_format.hpp> // for formattable enums to strings
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

        std::string message = fmt::vformat(format, args);

        auto textStyle = fmt::text_style();
        char const* severityString = "";

        switch (level) {
        case LogLevel::Fatal:
            severityString = "FATAL";
            textStyle = fmt::fg(fmt::color::black) | fmt::bg(fmt::color::red);
            break;
        case LogLevel::Error:
            severityString = "ERROR";
            textStyle = fmt::fg(fmt::color::red);
            break;
        case LogLevel::Warning:
            severityString = "WARNING";
            textStyle = fmt::fg(fmt::color::yellow);
            break;
        case LogLevel::Info:
            severityString = "INFO";
            textStyle = fmt::fg(fmt::color::white);
            break;
        case LogLevel::Verbose:
            severityString = "VERBOSE";
            textStyle = fmt::fg(fmt::color::light_gray);
            break;
        }

        // TODO: Maybe add some more logging context (e.g. function name)?
        fmt::print(textStyle, "[{}] {}\n", severityString, message);
    }
}

template<LogLevel level, typename... Args>
[[noreturn]] inline std::enable_if_t<level == LogLevel::Fatal> _internal_log(fmt::format_string<Args...> format, Args&&... args)
{
    _internal_vlog<level>(format, fmt::make_format_args(args...));

    #if !defined( ARKOSE_RELEASE )
    ARK_DEBUG_BREAK();
    #endif

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
