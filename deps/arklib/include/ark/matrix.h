/*
 * MIT License
 *
 * Copyright (c) 2020-2023 Simon Moos
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

#include "core.h"
#include "vector.h"

namespace ark {

template<typename T, typename _ = void>
struct tmat3 {
    tvec3<T> x, y, z;
};

template<typename T>
T* value_ptr(tmat3<T>& m)
{
    return value_ptr(m.x);
}

template<typename T>
const T* value_ptr(const tmat3<T>& m)
{
    return value_ptr(m.x);
}

template<typename T, typename _ = void>
struct tmat4 {
    tvec4<T> x, y, z, w;
};

template<typename T>
T* value_ptr(tmat4<T>& m)
{
    return value_ptr(m.x);
}

template<typename T>
const T* value_ptr(const tmat4<T>& m)
{
    return value_ptr(m.x);
}

template<typename T>
struct tmat3<T, ENABLE_STRUCT_IF_ARITHMETIC(T)> {
    tvec3<T> x, y, z;

    explicit tmat3(T d = static_cast<T>(1.0)) noexcept
        : x(d, static_cast<T>(0), static_cast<T>(0))
        , y(static_cast<T>(0), d, static_cast<T>(0))
        , z(static_cast<T>(0), static_cast<T>(0), d)
    {
    }

    tmat3(tvec3<T> x, tvec3<T> y, tvec3<T> z) noexcept
        : x(x)
        , y(y)
        , z(z)
    {
    }

    explicit tmat3(const tmat4<T>& m) noexcept
        : x(m.x.xyz())
        , y(m.y.xyz())
        , z(m.z.xyz())
    {
    }

    tvec3<T>& operator[](int index)
    {
        ARK_ASSERT(index >= 0);
        ARK_ASSERT(index < 3);
        tvec3<T>* v[] = { &x, &y, &z };
        return *v[index];
    }

    const tvec3<T>& operator[](int index) const
    {
        ARK_ASSERT(index >= 0);
        ARK_ASSERT(index < 3);
        const tvec3<T>* v[] = { &x, &y, &z };
        return *v[index];
    }

    constexpr tmat3<T> operator*(const tmat3<T>& other) const
    {
        tmat3<T> trans = transpose(*this);
        return {
            { dot(trans.x, other.x), dot(trans.y, other.x), dot(trans.z, other.x) },
            { dot(trans.x, other.y), dot(trans.y, other.y), dot(trans.z, other.y) },
            { dot(trans.x, other.z), dot(trans.y, other.z), dot(trans.z, other.z) }
        };
    }

    constexpr tvec3<T> operator*(const tvec3<T>& v) const
    {
        // TODO(optimization): Maybe make a version which doesn't require transpose first!
        tmat3<T> trans = transpose(*this);
        return { dot(trans.x, v),
                 dot(trans.y, v),
                 dot(trans.z, v) };
    }

    constexpr tmat3<T>
    operator*(T f) const
    {
        return { f * x, f * y, f * z };
    }

    constexpr bool operator==(const tmat3<T>& m)
    {
        return all(x == m.x) && all(y == m.y) && all(z == m.z);
    }

    constexpr bool operator!=(const tmat3<T>& m)
    {
        return !all(x == m.x) || !all(y == m.y) || !all(z == m.z);
    }
};

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr tmat3<T> operator*(T lhs, const tmat3<T>& rhs)
{
    return rhs * lhs;
}

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr tmat3<T> transpose(const tmat3<T>& m)
{
    return {
        { m.x.x, m.y.x, m.z.x },
        { m.x.y, m.y.y, m.z.y },
        { m.x.z, m.y.z, m.z.z }
    };
}

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr T determinant(const tmat3<T>& m)
{
    return m.x.x * (m.y.y * m.z.z - m.y.z * m.z.y)
        - m.y.x * (m.x.y * m.z.z - m.z.y * m.x.z)
        + m.z.x * (m.x.y * m.y.z - m.y.y * m.x.z);
}

template<typename T, ENABLE_IF_FLOATING_POINT(T)>
constexpr tmat3<T> inverse(const tmat3<T>& m)
{
    // This function is a rewritten version of  https://stackoverflow.com/a/18504573

    T det = determinant(m);
    if (std::abs(det) < std::numeric_limits<T>::epsilon()) {
        ARK_ON_BAD_DETERMINANT_IN_MATRIX_INVERSE();
    }
    T invDet = static_cast<T>(1) / det;

    tmat3<T> res;

    res.x.x = (m.y.y * m.z.z - m.y.z * m.z.y) * invDet;
    res.y.x = (m.z.x * m.y.z - m.y.x * m.z.z) * invDet;
    res.z.x = (m.y.x * m.z.y - m.z.x * m.y.y) * invDet;

    res.x.y = (m.z.y * m.x.z - m.x.y * m.z.z) * invDet;
    res.y.y = (m.x.x * m.z.z - m.z.x * m.x.z) * invDet;
    res.z.y = (m.x.y * m.z.x - m.x.x * m.z.y) * invDet;

    res.x.z = (m.x.y * m.y.z - m.x.z * m.y.y) * invDet;
    res.y.z = (m.x.z * m.y.x - m.x.x * m.y.z) * invDet;
    res.z.z = (m.x.x * m.y.y - m.x.y * m.y.x) * invDet;

    return res;
}

using mat3 = tmat3<Float>;
using fmat3 = tmat3<f32>;
using dmat3 = tmat3<f64>;

template<typename T>
struct tmat4<T, ENABLE_STRUCT_IF_ARITHMETIC(T)> {
    tvec4<T> x, y, z, w;

    explicit tmat4(T d = static_cast<T>(1)) noexcept
        : x(d, static_cast<T>(0), static_cast<T>(0), static_cast<T>(0))
        , y(static_cast<T>(0), d, static_cast<T>(0), static_cast<T>(0))
        , z(static_cast<T>(0), static_cast<T>(0), d, static_cast<T>(0))
        , w(static_cast<T>(0), static_cast<T>(0), static_cast<T>(0), d)
    {
    }

    tmat4(tvec4<T> x, tvec4<T> y, tvec4<T> z, tvec4<T> w) noexcept
        : x(x)
        , y(y)
        , z(z)
        , w(w)
    {
    }

    explicit tmat4(const tmat3<T>& m) noexcept
        : x(m.x, 0)
        , y(m.y, 0)
        , z(m.z, 0)
        , w(0, 0, 0, 1)
    {
    }

    tvec4<T>& operator[](int index)
    {
        ARK_ASSERT(index >= 0);
        ARK_ASSERT(index < 4);
        tvec4<T>* v[] = { &x, &y, &z, &w };
        return *v[index];
    }

    const tvec4<T>& operator[](int index) const
    {
        ARK_ASSERT(index >= 0);
        ARK_ASSERT(index < 4);
        const tvec4<T>* v[] = { &x, &y, &z, &w };
        return *v[index];
    }

    constexpr tmat4<T> operator*(const tmat4<T>& other) const
    {
        // TODO(optimization): It might be possible to make an even faster SIMD specialization for f32 of this whole thing, not just the vec4 dot products!
        tmat4<T> trans = transpose(*this);
        return {
            { dot(trans.x, other.x), dot(trans.y, other.x), dot(trans.z, other.x), dot(trans.w, other.x) },
            { dot(trans.x, other.y), dot(trans.y, other.y), dot(trans.z, other.y), dot(trans.w, other.y) },
            { dot(trans.x, other.z), dot(trans.y, other.z), dot(trans.z, other.z), dot(trans.w, other.z) },
            { dot(trans.x, other.w), dot(trans.y, other.w), dot(trans.z, other.w), dot(trans.w, other.w) }
        };
    }

    constexpr tvec4<T> operator*(const tvec4<T>& v) const
    {
        // TODO(optimization): Maybe make a version which doesn't require transpose first!
        tmat4<T> trans = transpose(*this);
        return { dot(trans.x, v),
                 dot(trans.y, v),
                 dot(trans.z, v),
                 dot(trans.w, v) };
    }

    constexpr tvec3<T> operator*(const tvec3<T>& v) const
    {
        // TODO(optimization): Maybe make a version which doesn't require transpose first!
        tmat4<T> trans = transpose(*this);
        return { dotVec4WithVec3ImplicitW1(trans.x, v),
                 dotVec4WithVec3ImplicitW1(trans.y, v),
                 dotVec4WithVec3ImplicitW1(trans.z, v) };
    }

    constexpr tmat4<T> operator*(T f) const
    {
        return { f * x, f * y, f * z, f * w };
    }

    constexpr bool operator==(const tmat4<T>& m)
    {
        return all(x == m.x) && all(y == m.y) && all(z == m.z) && all(w == m.w);
    }

    constexpr bool operator!=(const tmat4<T>& m)
    {
        return !all(x == m.x) || !all(y == m.y) || !all(z == m.z) || !all(w == m.w);
    }
};

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr tmat4<T> operator*(T lhs, const tmat4<T>& rhs)
{
    return rhs * lhs;
}

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr tmat4<T> transpose(const tmat4<T>& m)
{
    return {
        { m.x.x, m.y.x, m.z.x, m.w.x },
        { m.x.y, m.y.y, m.z.y, m.w.y },
        { m.x.z, m.y.z, m.z.z, m.w.z },
        { m.x.w, m.y.w, m.z.w, m.w.w }
    };
}

template<typename T, ENABLE_IF_FLOATING_POINT(T)>
constexpr tmat4<T> inverse(const tmat4<T>& m)
{
    // This function is a rewritten version of mat4x4_invert https://github.com/datenwolf/linmath.h

    T s[6];
    T c[6];

    s[0] = m.x.x * m.y.y - m.y.x * m.x.y;
    s[1] = m.x.x * m.y.z - m.y.x * m.x.z;
    s[2] = m.x.x * m.y.w - m.y.x * m.x.w;
    s[3] = m.x.y * m.y.z - m.y.y * m.x.z;
    s[4] = m.x.y * m.y.w - m.y.y * m.x.w;
    s[5] = m.x.z * m.y.w - m.y.z * m.x.w;

    c[0] = m.z.x * m.w.y - m.w.x * m.z.y;
    c[1] = m.z.x * m.w.z - m.w.x * m.z.z;
    c[2] = m.z.x * m.w.w - m.w.x * m.z.w;
    c[3] = m.z.y * m.w.z - m.w.y * m.z.z;
    c[4] = m.z.y * m.w.w - m.w.y * m.z.w;
    c[5] = m.z.z * m.w.w - m.w.z * m.z.w;

    T det = s[0] * c[5] - s[1] * c[4] + s[2] * c[3] + s[3] * c[2] - s[4] * c[1] + s[5] * c[0];
    if (std::abs(det) < std::numeric_limits<T>::epsilon()) {
        ARK_ON_BAD_DETERMINANT_IN_MATRIX_INVERSE();
    }
    T invDet = static_cast<T>(1) / det;

    tmat4<T> res;

    res.x.x = (m.y.y * c[5] - m.y.z * c[4] + m.y.w * c[3]) * invDet;
    res.x.y = (-m.x.y * c[5] + m.x.z * c[4] - m.x.w * c[3]) * invDet;
    res.x.z = (m.w.y * s[5] - m.w.z * s[4] + m.w.w * s[3]) * invDet;
    res.x.w = (-m.z.y * s[5] + m.z.z * s[4] - m.z.w * s[3]) * invDet;

    res.y.x = (-m.y.x * c[5] + m.y.z * c[2] - m.y.w * c[1]) * invDet;
    res.y.y = (m.x.x * c[5] - m.x.z * c[2] + m.x.w * c[1]) * invDet;
    res.y.z = (-m.w.x * s[5] + m.w.z * s[2] - m.w.w * s[1]) * invDet;
    res.y.w = (m.z.x * s[5] - m.z.z * s[2] + m.z.w * s[1]) * invDet;

    res.z.x = (m.y.x * c[4] - m.y.y * c[2] + m.y.w * c[0]) * invDet;
    res.z.y = (-m.x.x * c[4] + m.x.y * c[2] - m.x.w * c[0]) * invDet;
    res.z.z = (m.w.x * s[4] - m.w.y * s[2] + m.w.w * s[0]) * invDet;
    res.z.w = (-m.z.x * s[4] + m.z.y * s[2] - m.z.w * s[0]) * invDet;

    res.w.x = (-m.y.x * c[3] + m.y.y * c[1] - m.y.z * c[0]) * invDet;
    res.w.y = (m.x.x * c[3] - m.x.y * c[1] + m.x.z * c[0]) * invDet;
    res.w.z = (-m.w.x * s[3] + m.w.y * s[1] - m.w.z * s[0]) * invDet;
    res.w.w = (m.z.x * s[3] - m.z.y * s[1] + m.z.z * s[0]) * invDet;

    return res;
}

using mat4 = tmat4<Float>;
using fmat4 = tmat4<f32>;
using dmat4 = tmat4<f64>;

template<typename T>
struct tmat3x4 {
    tvec4<T> x, y, z;

    tmat3x4() noexcept
        : x()
        , y()
        , z()
    {
    }

    tmat3x4(const tmat4<T>& m) noexcept
        : x(m.x)
        , y(m.y)
        , z(m.z)
    {
    }
};

template<typename T>
T* value_ptr(tmat3x4<T>& m)
{
    return value_ptr(m.x);
}

template<typename T>
const T* value_ptr(const tmat3x4<T>& m)
{
    return value_ptr(m.x);
}

using mat3x4 = tmat3x4<Float>;
using fmat3x4 = tmat3x4<f32>;
using dmat3x4 = tmat3x4<f64>;

} // namespace ark

#ifndef ARK_DONT_EXPOSE_COMMON_MATH_TYPES
using mat3 = ark::mat3;
using mat4 = ark::mat4;
using mat3x4 = ark::mat3x4;
#endif
