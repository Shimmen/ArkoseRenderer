#pragma once

#if defined(_MSC_VER)
 #include <intrin.h>
 #define DEBUG_BREAK() __debugbreak()
#else
 #warning "No implementation for DEBUG_BREAK() for this platform!"
 #define DEBUG_BREAK()
#endif
