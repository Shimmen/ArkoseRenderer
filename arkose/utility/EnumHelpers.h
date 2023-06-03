#pragma once

#include <type_traits>

template<typename E>
constexpr typename std::underlying_type<E>::type toUnderlying(E e) noexcept
{
    return static_cast<std::underlying_type_t<E>>(e);
}

#define ARKOSE_ENUM_CLASS_BIT_FLAGS(T)                                                                                                                                               \
    inline bool isSet(T a) { return static_cast<std::underlying_type_t<T>>(a) != 0; }                                                                                                \
    inline T operator~(T a) { return static_cast<T>(~static_cast<std::underlying_type_t<T>>(a)); }                                                                                   \
    inline T operator|(T a, T b) { return static_cast<T>(static_cast<std::underlying_type_t<T>>(a) | static_cast<std::underlying_type_t<T>>(b)); }                                   \
    inline T operator&(T a, T b) { return static_cast<T>(static_cast<std::underlying_type_t<T>>(a) & static_cast<std::underlying_type_t<T>>(b)); }                                   \
    inline T operator^(T a, T b) { return static_cast<T>(static_cast<std::underlying_type_t<T>>(a) ^ static_cast<std::underlying_type_t<T>>(b)); }                                // \
    //inline T& operator|=(T& a, T b) { return static_cast<T&>(static_cast<std::add_lvalue_reference_t<std::underlying_type_t<T>>>(a) |= static_cast<std::underlying_type_t<T>>(b)); } \
    //inline T& operator&=(T& a, T b) { return static_cast<T&>(static_cast<std::add_lvalue_reference_t<std::underlying_type_t<T>>>(a) &= static_cast<std::underlying_type_t<T>>(b)); } \
    //inline T& operator^=(T& a, T b) { return static_cast<T&>(static_cast<std::add_lvalue_reference_t<std::underlying_type_t<T>>>(a) ^= static_cast<std::underlying_type_t<T>>(b)); }

