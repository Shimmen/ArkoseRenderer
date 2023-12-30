#pragma once

#include <ark/compiler.h> // for ARK_FORCE_INLINE
#include <ark/debugger.h> // for ARK_DEBUG_BREAK()
#include <cstdlib> // for exit()
#include <fmt/format.h> // for fmt::format()
#include <core/Logging.h> // for ARKOSE_LOG()

#if defined(_MSC_VER)
#include <Windows.h>
#endif

ARK_FORCE_INLINE void ArkoseAssertHandler(char const* assertion, char const* filename, int line)
{
    std::string assertionMessage = fmt::format("Assertion failed: '{}' ({} line {})", assertion, filename, line);
    ARKOSE_LOG(Error, "{}", assertionMessage);

#if defined(_MSC_VER)
    if (IsDebuggerPresent()) {
        std::string messageBoxMessage = fmt::format("Assertion failed: '{}'\nIn file {} on line {}\nDo you want to break?", assertion, filename, line);
        switch (MessageBoxA(NULL, messageBoxMessage.c_str(), "Assertion failed!", MB_ABORTRETRYIGNORE)) {
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

// NOTE: This triggers in both debug & release modes!
#define ARKOSE_ASSERT(expression)                                 \
    (void)(                                                       \
        (!!(expression)) ||                                       \
        (ArkoseAssertHandler(#expression, __FILE__, __LINE__), 0) \
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
