/*
 * MIT License
 *
 * Copyright (c) 2020-2022 Simon Moos
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include <algorithm> // for std::min/max etc.
#include <cassert> // for the assert macro
#include <cmath> // for basic math functions
#include <cstdint> // for integer definitions
#include <type_traits> // for std::enable_if etc.

#ifndef ARK_NO_INTRINSICS
#include <intrin.h>
#endif

namespace ark {

// Options

// Redefine this to use any assert
#ifndef ARK_ASSERT
#define ARK_ASSERT(x) assert(x)
#endif

// Some types assume a default float precision or don't allow choosing precision per object,
// but instead globally. For these cases this option exist. By default a 32-bit float is used.
#ifdef ARK_USE_DOUBLE_BY_DEFAULT
using Float = double;
#else
using Float = float;
#endif

// By default including e.g. vector.h will expose ark::vec3 (and more) to the global namespace.
// This makes it very convenient, I find, in cases where math code is found everywhere, e.g. in
// a renderer. However, it might not suitable in all cases, so define this to avoid polluting the
// namespace.
#ifdef ARK_DONT_EXPOSE_COMMON_MATH_TYPES
#endif

// When inverting a matrix we have to divide by the determinant, which may be zero. The
// redefine this macro to specify some custom behaviour to handle this divide by zero case.
#ifndef ARK_ON_BAD_DETERMINANT_IN_MATRIX_INVERSE
#define ARK_ON_BAD_DETERMINANT_IN_MATRIX_INVERSE() ARK_ASSERT(false)
#endif

// Explicit numeric types

using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using f32 = float;
using f64 = double;

// Utility definitions

using size_t = std::size_t;

// Utilities & macros

#define ENABLE_STRUCT_IF_ARITHMETIC(T) typename std::enable_if<std::is_arithmetic<T>::value>::type
#define ENABLE_STRUCT_IF_FLOATING_POINT(T) typename std::enable_if<std::is_floating_point<T>::value>::type
#define ENABLE_STRUCT_IF_INTEGRAL(T) typename std::enable_if<std::is_integral<T>::value>::type

#define ENABLE_IF_ARITHMETIC(T) typename = typename std::enable_if<std::is_arithmetic<T>::value>::type
#define ENABLE_IF_FLOATING_POINT(T) typename = typename std::enable_if<std::is_floating_point<T>::value>::type
#define ENABLE_IF_INTEGRAL(T) typename = typename std::enable_if<std::is_integral<T>::value>::type

// Math constants & basic math functions

constexpr Float E = static_cast<Float>(2.718281828459);
constexpr Float PI = static_cast<Float>(3.141592653590);
constexpr Float HALF_PI = PI / static_cast<Float>(2.0);
constexpr Float TWO_PI = static_cast<Float>(2.0) * PI;

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr T square(T x)
{
    return x * x;
}

template<typename T, ENABLE_IF_INTEGRAL(T)>
constexpr bool isPowerOfTwo(T x)
{
    return x > 0 && (x & (x - 1)) == 0;
}

template<typename T, ENABLE_IF_FLOATING_POINT(T)>
constexpr T lerp(T a, T b, T x)
{
    return (static_cast<T>(1) - x) * a + x * b;
}

template<typename T, ENABLE_IF_FLOATING_POINT(T)>
constexpr T fract(T x)
{
    return x - std::floor(x);
}

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr T clamp(T x, T min, T max)
{
    return std::max(min, std::min(x, max));
}

constexpr Float toRadians(Float degrees)
{
    return degrees / static_cast<Float>(180.0) * PI;
}

constexpr Float toDegrees(Float radians)
{
    return radians / PI * static_cast<Float>(180.0);
}

template<typename T, ENABLE_IF_INTEGRAL(T)>
constexpr T divideAndRoundUp(T numerator, T denominator)
{
    return (numerator + denominator - 1) / denominator;
}

} // namespace ark
