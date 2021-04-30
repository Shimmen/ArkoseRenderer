#pragma once

///////////////////////////////////////////////////////////////////////////////
// Assert & similar

#include <cassert>
#define ASSERT(x) assert(x)

#define ASSERT_NOT_REACHED()                  \
    do {                                      \
        ASSERT(false);                        \
        exit(0); /* for noreturn behaviour */ \
    } while (false)

#define NOT_YET_IMPLEMENTED()                 \
    do {                                      \
        ASSERT(false);                        \
        exit(0); /* for noreturn behaviour */ \
    } while (false)

///////////////////////////////////////////////////////////////////////////////
// Scoped exit, i.e. kind of like defer but instead of function scopes it works for all scopes
// From: http://the-witness.net/news/2012/11/scopeexit-in-c11/

template<typename Func>
class AtScopeExit {
public:
    
    explicit AtScopeExit(Func&& func)
        : m_function(func)
    {
    }

    ~AtScopeExit()
    {
        m_function();
    }

private:
    Func m_function;
};
