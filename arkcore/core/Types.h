#pragma once

#include "core/Assert.h"
#include <ark/core.h>

using i8 = ark::i8;
using i16 = ark::i16;
using i32 = ark::i32;
using i64 = ark::i64;

using u8 = ark::u8;
using u16 = ark::u16;
using u32 = ark::u32;
using u64 = ark::u64;

using f32 = float;
using f64 = double;

// Useful for GLSL interopt
using uint = uint32_t;

template<typename NarrowType, typename WideType>
constexpr NarrowType narrow_cast(WideType);

template<>
constexpr u32 narrow_cast(u64 wideValue)
{
    ARKOSE_ASSERT(wideValue <= static_cast<u64>(UINT32_MAX));
    return static_cast<u32>(wideValue);
}

template<>
constexpr u16 narrow_cast(u32 wideValue)
{
    ARKOSE_ASSERT(wideValue <= static_cast<u32>(UINT16_MAX));
    return static_cast<u16>(wideValue);
}

template<>
constexpr u8 narrow_cast(u32 wideValue)
{
    ARKOSE_ASSERT(wideValue <= static_cast<u32>(UINT8_MAX));
    return static_cast<u8>(wideValue);
}

#if defined(__clang__)
template<>
constexpr u32 narrow_cast(size_t wideValue)
{
    ARKOSE_ASSERT(wideValue <= static_cast<size_t>(UINT32_MAX));
    return static_cast<u32>(wideValue);
}
#endif
