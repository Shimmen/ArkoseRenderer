#pragma once

#include <ark/compiler.h> // for ARK_FORCE_INLINE
#include <ark/debugger.h> // for ARK_DEBUG_BREAK()
#include <cstdlib> // for exit()
#include <fmt/format.h> // for fmt::format()
#include <core/Logging.h> // for ARKOSE_LOG()

#if defined(_MSC_VER)
#include <Windows.h>
#endif

ARK_FORCE_INLINE void ArkoseAssertHandlerImpl(char const* assertion, char const* filename, int line, fmt::string_view format, fmt::format_args args)
{
    std::string optionalMessage = "";
    if (format.size() > 0) {
        optionalMessage = fmt::vformat(format, args);
        optionalMessage += '\n';
    }

    std::string assertionMessage;
    if (assertion != nullptr) {
        assertionMessage = fmt::format("Assertion failed: '{}'\nIn file {} on line {}\n{}Do you want to break?", assertion, filename, line, optionalMessage);
    } else {
        assertionMessage = fmt::format("Error!\nIn file {} on line {}\n{}Do you want to break?", filename, line, optionalMessage);
    }

#if defined(_MSC_VER)
    if (IsDebuggerPresent()) {
        switch (MessageBoxA(NULL, assertionMessage.c_str(), "Assertion failed!", MB_ABORTRETRYIGNORE)) {
        case IDABORT:
            exit(EXIT_FAILURE);
            break;
        case IDRETRY:
            // TODO: Somehow issue the debug break inside the macro so that we don't include this function in the callstack
            ARK_DEBUG_BREAK();
            break;
        case IDIGNORE:
            // do nothing
            break;
        }
    }
#else
    // TODO: Add better platform implementations!
    ARK_DEBUG_BREAK();
#endif
}

template<typename... Args>
inline void ArkoseAssertHandler(char const* assertion, char const* filename, int line, fmt::format_string<Args...> format, Args&&... args)
{
    ArkoseAssertHandlerImpl(assertion, filename, line, format, fmt::make_format_args(args...));
}

#if defined(ARKOSE_RELEASE)

#define ARKOSE_ASSERT(expression) (void)(expression)
#define ARKOSE_ASSERTM(expression, format, ...) (void)(expression)
#define ARKOSE_ERROR(format, ...)
#define ASSERT_NOT_REACHED() exit(EXIT_FAILURE)
#define NOT_YET_IMPLEMENTED() exit(EXIT_FAILURE)

#else

#define ARKOSE_ASSERT(expression)                                                                  \
    (void)(                                                                                        \
        (!!(expression)) ||                                                                        \
        (ArkoseAssertHandler(#expression, __FILE__, __LINE__, FMT_STRING("")), 0)                  \
    )

#define ARKOSE_ASSERTM(expression, format, ...)                                                                 \
    (void)(                                                                                                     \
        (!!(expression)) ||                                                                                     \
        (ArkoseAssertHandler(#expression, __FILE__, __LINE__, FMT_STRING(format) __VA_OPT__(,) __VA_ARGS__), 0) \
    )

#define ARKOSE_ERROR(format, ...)                                                                               \
    (void)(                                                                                                     \
        (ArkoseAssertHandler(nullptr, __FILE__, __LINE__, FMT_STRING(format) __VA_OPT__(,) __VA_ARGS__), 0)     \
    )

#define ASSERT_NOT_REACHED()  \
    do {                      \
        ARKOSE_ASSERT(false); \
        exit(EXIT_FAILURE);   \
    } while (false)

#define NOT_YET_IMPLEMENTED() \
    do {                      \
        ARKOSE_ASSERT(false); \
        exit(EXIT_FAILURE);   \
    } while (false)

#endif
