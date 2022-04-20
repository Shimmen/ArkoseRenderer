#pragma once

#include <cassert> // for assert
#include <cstdlib> // for exit, for noreturn behaviour

#define ARKOSE_ASSERT(x) assert(x)

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
