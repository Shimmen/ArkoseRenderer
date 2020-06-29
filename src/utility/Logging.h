#pragma once

#include "util.h"
#include <cstdarg>
#include <cstdio>

enum class LogLevel {
    None = 0,
    Error,
    Warning,
    Info,
    All
};

constexpr LogLevel currentLogLevel = LogLevel::Info;

inline void LogInfo(const char* format, ...)
{
    if constexpr (currentLogLevel < LogLevel::Info)
        return;

    va_list vaList;
    va_start(vaList, format);
    vfprintf(stdout, format, vaList);
    fflush(stdout);
    va_end(vaList);
}

inline void LogWarning(const char* format, ...)
{
    if constexpr (currentLogLevel < LogLevel::Warning)
        return;

    va_list vaList;
    va_start(vaList, format);
    vfprintf(stderr, format, vaList);
    fflush(stderr);
    va_end(vaList);
}

inline void LogError(const char* format, ...)
{
    if constexpr (currentLogLevel < LogLevel::Error)
        return;

    va_list vaList;
    va_start(vaList, format);
    vfprintf(stderr, format, vaList);
    fflush(stderr);
    va_end(vaList);
}

[[noreturn]] inline void LogErrorAndExit(const char* format, ...)
{
    if constexpr (currentLogLevel >= LogLevel::Error) {
        va_list vaList;
        va_start(vaList, format);
        vfprintf(stderr, format, vaList);
        fflush(stderr);
        va_end(vaList);
    }
#ifdef _MSC_VER
    __debugbreak();
#endif
    exit(123);
}
