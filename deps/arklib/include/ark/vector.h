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

namespace ark {

template<typename T, typename _ = void>
struct tvec2 {
    T x, y;
};

template<typename T>
T* value_ptr(tvec2<T>& v)
{
    return &v.x;
}

template<typename T>
const T* value_ptr(const tvec2<T>& v)
{
    return &v.x;
}

template<typename T, typename _ = void>
struct tvec3 {
    T x, y, z;
};

template<typename T>
T* value_ptr(tvec3<T>& v)
{
    return &v.x;
}

template<typename T>
const T* value_ptr(const tvec3<T>& v)
{
    return &v.x;
}

template<typename T, typename _ = void>
struct tvec4 {
    T x, y, z, w;
};

template<typename T>
T* value_ptr(tvec4<T>& v)
{
    return &v.x;
}

template<typename T>
const T* value_ptr(const tvec4<T>& v)
{
    return &v.x;
}

template<typename T>
struct tvec2<T, ENABLE_STRUCT_IF_ARITHMETIC(T)> {
    T x, y;

    constexpr tvec2(T x, T y) noexcept
        : x(x)
        , y(y)
    {
    }

    explicit constexpr tvec2(T e) noexcept
        : tvec2(e, e)
    {
    }

    constexpr tvec2() noexcept
        : tvec2(static_cast<T>(0))
    {
    }

    bool operator==(const tvec2& other) const
    {
        return x == other.x && y == other.y;
    }

    T& operator[](int index)
    {
        ARK_ASSERT(index >= 0);
        ARK_ASSERT(index < 2);
        T* v[] = { &x, &y };
        return *v[index];
    }

    const T& operator[](int index) const
    {
        ARK_ASSERT(index >= 0);
        ARK_ASSERT(index < 2);
        const T* v[] = { &x, &y };
        return *v[index];
    }

    constexpr tvec2<T> operator+() const { return *this; }
    constexpr tvec2<T> operator-() const { return { -x, -y }; }

    constexpr tvec2<T> operator+(const tvec2<T>& v) const { return { x + v.x, y + v.y }; }
    constexpr tvec2<T>& operator+=(const tvec2<T>& v)
    {
        x += v.x;
        y += v.y;
        return *this;
    }

    constexpr tvec2<T> operator-(const tvec2<T>& v) const { return { x - v.x, y - v.y }; }
    constexpr tvec2<T>& operator-=(const tvec2<T>& v)
    {
        x -= v.x;
        y -= v.y;
        return *this;
    }

    constexpr tvec2<T> operator*(const tvec2<T>& v) const { return { x * v.x, y * v.y }; }
    constexpr tvec2<T>& operator*=(const tvec2<T>& v)
    {
        x *= v.x;
        y *= v.y;
        return *this;
    }

    constexpr tvec2<T> operator/(const tvec2<T>& v) const { return { x / v.x, y / v.y }; }
    constexpr tvec2<T>& operator/=(const tvec2<T>& v)
    {
        x /= v.x;
        y /= v.y;
        return *this;
    }
    constexpr tvec2<T> operator*(T f) const { return { x * f, y * f }; }
    constexpr tvec2<T>& operator*=(T f)
    {
        x *= f;
        y *= f;
        return *this;
    }

    constexpr tvec2<T> operator/(T f) const { return { x / f, y / f }; }
    constexpr tvec2<T>& operator/=(T f)
    {
        x /= f;
        y /= f;
        return *this;
    }
};

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr tvec2<T> operator*(T lhs, const tvec2<T>& rhs)
{
    return rhs * lhs;
}

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr T dot(const tvec2<T>& lhs, const tvec2<T>& rhs)
{
    return lhs.x * rhs.x + lhs.y * rhs.y;
}

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr T length2(const tvec2<T>& v)
{
    return dot(v, v);
}

template<typename T, ENABLE_IF_FLOATING_POINT(T)>
constexpr T length(const tvec2<T>& v)
{
    return std::sqrt(length2(v));
}

template<typename T, ENABLE_IF_FLOATING_POINT(T)>
constexpr T distance(const tvec2<T>& a, const tvec2<T>& b)
{
    return length(a - b);
}

template<typename T, ENABLE_IF_FLOATING_POINT(T)>
constexpr tvec2<T> normalize(const tvec2<T>& v)
{
    return v / length(v);
}

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr tvec2<T> abs(const tvec2<T>& v)
{
    return { std::abs(v.x), std::abs(v.y) };
}

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr tvec2<T> min(const tvec2<T>& lhs, const tvec2<T>& rhs)
{
    return { std::min(lhs.x, rhs.x), std::min(lhs.y, rhs.y) };
}

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr tvec2<T> max(const tvec2<T>& lhs, const tvec2<T>& rhs)
{
    return { std::max(lhs.x, rhs.x), std::max(lhs.y, rhs.y) };
}

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr T minComponent(const tvec2<T>& v)
{
    return std::min(v.x, v.y);
}

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr T maxComponent(const tvec2<T>& v)
{
    return std::max(v.x, v.y);
}

template<typename T, ENABLE_IF_FLOATING_POINT(T)>
constexpr tvec2<T> lerp(const tvec2<T>& a, const tvec2<T>& b, T x)
{
    return (static_cast<T>(1) - x) * a + x * b;
}

template<typename T, ENABLE_IF_FLOATING_POINT(T)>
constexpr tvec2<T> inverseLerp(const tvec2<T>& x, const tvec2<T>& a, const tvec2<T>& b)
{
    return (x - a) / (b - a);
}

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr tvec2<T> clamp(const tvec2<T>& x, const tvec2<T>& minEdge, const tvec2<T>& maxEdge)
{
    return max(minEdge, min(x, maxEdge));
}

template<>
struct tvec2<bool> {
    bool x, y;

    constexpr tvec2(bool x, bool y) noexcept
        : x(x)
        , y(y)
    {
    }

    explicit constexpr tvec2(bool e = false) noexcept
        : tvec2(e, e)
    {
    }

    constexpr tvec2<bool> operator~() const { return { !x, !y }; }
    constexpr tvec2<bool> operator||(const tvec2<bool>& v) const { return { x || v.x, y || v.y }; }
    constexpr tvec2<bool> operator&&(const tvec2<bool>& v) const { return { x && v.x, y && v.y }; }
};

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr tvec2<bool> lessThan(const tvec2<T>& lhs, const tvec2<T>& rhs)
{
    return { lhs.x < rhs.x, lhs.y < rhs.y };
}

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr tvec2<bool> lessThanEqual(const tvec2<T>& lhs, const tvec2<T>& rhs)
{
    return { lhs.x <= rhs.x, lhs.y <= rhs.y };
}

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr tvec2<bool> greaterThan(const tvec2<T>& lhs, const tvec2<T>& rhs)
{
    return { lhs.x > rhs.x, lhs.y > rhs.y };
}

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr tvec2<bool> greaterThanEqual(const tvec2<T>& lhs, const tvec2<T>& rhs)
{
    return { lhs.x >= rhs.x, lhs.y >= rhs.y };
}

constexpr bool any(const tvec2<bool>& v)
{
    return v.x || v.y;
}

constexpr bool all(const tvec2<bool>& v)
{
    return v.x && v.y;
}

using vec2 = tvec2<Float>;
using fvec2 = tvec2<f32>;
using dvec2 = tvec2<f64>;
using uvec2 = tvec2<u32>;
using ivec2 = tvec2<i32>;
using bvec2 = tvec2<bool>;

template<>
struct tvec3<bool> {
    bool x, y, z;

    constexpr tvec3(bool x, bool y, bool z) noexcept
        : x(x)
        , y(y)
        , z(z)
    {
    }

    explicit constexpr tvec3(bool e = false) noexcept
        : tvec3(e, e, e)
    {
    }

    constexpr tvec3<bool> operator~() const { return { !x, !y, !z }; }
    constexpr tvec3<bool> operator||(const tvec3<bool>& v) const { return { x || v.x, y || v.y, z || v.z }; }
    constexpr tvec3<bool> operator&&(const tvec3<bool>& v) const { return { x && v.x, y && v.y, z && v.z }; }
};

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr tvec3<bool> lessThan(const tvec3<T>& lhs, const tvec3<T>& rhs)
{
    return { lhs.x < rhs.x, lhs.y < rhs.y, lhs.z < rhs.z };
}

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr tvec3<bool> lessThanEqual(const tvec3<T>& lhs, const tvec3<T>& rhs)
{
    return { lhs.x <= rhs.x, lhs.y <= rhs.y, lhs.z <= rhs.z };
}

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr tvec3<bool> greaterThan(const tvec3<T>& lhs, const tvec3<T>& rhs)
{
    return { lhs.x > rhs.x, lhs.y > rhs.y, lhs.z > rhs.z };
}

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr tvec3<bool> greaterThanEqual(const tvec3<T>& lhs, const tvec3<T>& rhs)
{
    return { lhs.x >= rhs.x, lhs.y >= rhs.y, lhs.z >= rhs.z };
}

constexpr bool any(const tvec3<bool>& v)
{
    return v.x || v.y || v.z;
}

constexpr bool all(const tvec3<bool>& v)
{
    return v.x && v.y && v.z;
}

template<typename T>
struct tvec3<T, ENABLE_STRUCT_IF_ARITHMETIC(T)> {
    T x, y, z;

    constexpr tvec3(T x, T y, T z) noexcept
        : x(x)
        , y(y)
        , z(z)
    {
    }

    explicit constexpr tvec3(T e) noexcept
        : tvec3(e, e, e)
    {
    }

    constexpr tvec3() noexcept
        : tvec3(static_cast<T>(0))
    {
    }

    explicit constexpr tvec3(const tvec4<T> v) noexcept
        : tvec3(v.x, v.y, v.z)
    {
    }

    T& operator[](int index)
    {
        ARK_ASSERT(index >= 0);
        ARK_ASSERT(index < 3);
        T* v[] = { &x, &y, &z };
        return *v[index];
    }

    const T& operator[](int index) const
    {
        ARK_ASSERT(index >= 0);
        ARK_ASSERT(index < 3);
        const T* v[] = { &x, &y, &z };
        return *v[index];
    }

    constexpr tvec3<T> operator+() const { return *this; }
    constexpr tvec3<T> operator-() const { return { -x, -y, -z }; }

    constexpr tvec3<T> operator+(T t) const { return { x + t, y + t, z + t }; }
    constexpr tvec3<T> operator+(const tvec3<T>& v) const { return { x + v.x, y + v.y, z + v.z }; }
    constexpr tvec3<T>& operator+=(const tvec3<T>& v)
    {
        x += v.x;
        y += v.y;
        z += v.z;
        return *this;
    }

    constexpr tvec3<T> operator-(T t) const { return { x - t, y - t, z - t }; }
    constexpr tvec3<T> operator-(const tvec3<T>& v) const { return { x - v.x, y - v.y, z - v.z }; }
    constexpr tvec3<T>& operator-=(const tvec3<T>& v)
    {
        x -= v.x;
        y -= v.y;
        z -= v.z;
        return *this;
    }

    constexpr tvec3<T> operator*(const tvec3<T>& v) const { return { x * v.x, y * v.y, z * v.z }; }
    constexpr tvec3<T>& operator*=(const tvec3<T>& v)
    {
        x *= v.x;
        y *= v.y;
        z *= v.z;
        return *this;
    }

    constexpr tvec3<T> operator/(const tvec3<T>& v) const { return { x / v.x, y / v.y, z / v.z }; }
    constexpr tvec3<T>& operator/=(const tvec3<T>& v)
    {
        x /= v.x;
        y /= v.y;
        z /= v.z;
        return *this;
    }

    constexpr tvec3<T> operator*(T f) const { return { x * f, y * f, z * f }; }
    constexpr tvec3<T>& operator*=(T f)
    {
        x *= f;
        y *= f;
        z *= f;
        return *this;
    }

    constexpr tvec3<T> operator/(T f) const { return { x / f, y / f, z / f }; }
    constexpr tvec3<T>& operator/=(T f)
    {
        x /= f;
        y /= f;
        z /= f;
        return *this;
    }

    constexpr tvec3<bool> operator==(const tvec3<T>& v) const
    {
        return tvec3<bool>(x == v.x, y == v.y, z == v.z);
    }

    constexpr tvec3<bool> operator!=(const tvec3<T>& v) const
    {
        return tvec3<bool>(x != v.x, y != v.y, z != v.z);
    }
};

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr tvec3<T> operator*(T lhs, const tvec3<T>& rhs)
{
    return rhs * lhs;
}

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr tvec3<T> cross(const tvec3<T>& lhs, const tvec3<T>& rhs)
{
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x
    };
}

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr T dot(const tvec3<T>& lhs, const tvec3<T>& rhs)
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr T length2(const tvec3<T>& v)
{
    return dot(v, v);
}

template<typename T, ENABLE_IF_FLOATING_POINT(T)>
constexpr T length(const tvec3<T>& v)
{
    return std::sqrt(length2(v));
}

template<typename T, ENABLE_IF_FLOATING_POINT(T)>
constexpr T distance(const tvec3<T>& a, const tvec3<T>& b)
{
    return length(a - b);
}

template<typename T, ENABLE_IF_FLOATING_POINT(T)>
constexpr tvec3<T> normalize(const tvec3<T>& v)
{
    return v / length(v);
}

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr tvec3<T> abs(const tvec3<T>& v)
{
    return { std::abs(v.x), std::abs(v.y), std::abs(v.z) };
}

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr tvec3<T> min(const tvec3<T>& lhs, const tvec3<T>& rhs)
{
    return { std::min(lhs.x, rhs.x), std::min(lhs.y, rhs.y), std::min(lhs.z, rhs.z) };
}

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr tvec3<T> max(const tvec3<T>& lhs, const tvec3<T>& rhs)
{
    return { std::max(lhs.x, rhs.x), std::max(lhs.y, rhs.y), std::max(lhs.z, rhs.z) };
}

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr T minComponent(const tvec3<T>& v)
{
    return std::min(v.x, std::min(v.y, v.z));
}

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr T maxComponent(const tvec3<T>& v)
{
    return std::max(v.x, std::max(v.y, v.z));
}

template<typename T, ENABLE_IF_FLOATING_POINT(T)>
constexpr tvec3<T> lerp(const tvec3<T>& a, const tvec3<T>& b, T x)
{
    return (static_cast<T>(1) - x) * a + x * b;
}

template<typename T, ENABLE_IF_FLOATING_POINT(T)>
constexpr tvec3<T> inverseLerp(const tvec3<T>& x, const tvec3<T>& a, const tvec3<T>& b)
{
    return (x - a) / (b - a);
}


template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr tvec3<T> clamp(const tvec3<T>& x, const tvec3<T>& minEdge, const tvec3<T>& maxEdge)
{
    return max(minEdge, min(x, maxEdge));
}

using vec3 = tvec3<Float>;
using fvec3 = tvec3<f32>;
using dvec3 = tvec3<f64>;
using uvec3 = tvec3<u32>;
using ivec3 = tvec3<i32>;
using bvec3 = tvec3<bool>;

template<>
struct tvec4<bool> {
    bool x, y, z, w;

    constexpr tvec4(bool x, bool y, bool z, bool w) noexcept
        : x(x)
        , y(y)
        , z(z)
        , w(w)
    {
    }

    explicit constexpr tvec4(bool e = false) noexcept
        : tvec4(e, e, e, e)
    {
    }

    constexpr tvec4<bool> operator~() const { return { !x, !y, !z, !w }; }
    constexpr tvec4<bool> operator||(const tvec4<bool>& v) const { return { x || v.x, y || v.y, z || v.z, w || v.w }; }
    constexpr tvec4<bool> operator&&(const tvec4<bool>& v) const { return { x && v.x, y && v.y, z && v.z, w && v.w }; }
};

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr tvec4<bool> lessThan(const tvec4<T>& lhs, const tvec4<T>& rhs)
{
    return { lhs.x < rhs.x, lhs.y < rhs.y, lhs.z < rhs.z, lhs.w < rhs.w };
}

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr tvec4<bool> lessThanEqual(const tvec4<T>& lhs, const tvec4<T>& rhs)
{
    return { lhs.x <= rhs.x, lhs.y <= rhs.y, lhs.z <= rhs.z, lhs.w <= rhs.w };
}

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr tvec4<bool> greaterThan(const tvec4<T>& lhs, const tvec4<T>& rhs)
{
    return { lhs.x > rhs.x, lhs.y > rhs.y, lhs.z > rhs.z, lhs.w > rhs.w };
}

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr tvec4<bool> greaterThanEqual(const tvec4<T>& lhs, const tvec4<T>& rhs)
{
    return { lhs.x >= rhs.x, lhs.y >= rhs.y, lhs.z >= rhs.z, lhs.w >= rhs.w };
}

constexpr bool any(const tvec4<bool>& v)
{
    return v.x || v.y || v.z || v.w;
}

constexpr bool all(const tvec4<bool>& v)
{
    return v.x && v.y && v.z && v.w;
}

template<typename T>
struct tvec4<T, ENABLE_STRUCT_IF_ARITHMETIC(T)> {
    T x, y, z, w;

    constexpr tvec4(T x, T y, T z, T w) noexcept
        : x(x)
        , y(y)
        , z(z)
        , w(w)
    {
    }

    constexpr tvec4() noexcept
        : tvec4(0, 0, 0, 0)
    {
    }

    explicit constexpr tvec4(T e) noexcept
        : tvec4(e, e, e, e)
    {
    }

    constexpr tvec4(const tvec2<T>& v, T z, T w) noexcept
        : tvec4(v.x, v.y, z, w)
    {
    }

    constexpr tvec4(const tvec2<T>& v1, const tvec2<T>& v2) noexcept
        : tvec4(v1.x, v1.y, v2.x, v2.y)
    {
    }

    constexpr tvec4(const tvec3<T>& v, T w) noexcept
        : tvec4(v.x, v.y, v.z, w)
    {
    }

    T& operator[](int index)
    {
        ARK_ASSERT(index >= 0);
        ARK_ASSERT(index < 4);
        T* v[] = { &x, &y, &z, &w };
        return *v[index];
    }

    const T& operator[](int index) const
    {
        ARK_ASSERT(index >= 0);
        ARK_ASSERT(index < 4);
        const T* v[] = { &x, &y, &z, &w };
        return *v[index];
    }

    constexpr tvec4<T> operator+() const { return *this; }
    constexpr tvec4<T> operator-() const { return { -x, -y, -z, -w }; }

    constexpr tvec4<T> operator+(T t) const { return { x + t, y + t, z + t, w + t }; }
    constexpr tvec4<T> operator+(const tvec4<T>& v) const { return { x + v.x, y + v.y, z + v.z, w + v.w }; }
    constexpr tvec4<T>& operator+=(const tvec4<T>& v)
    {
        x += v.x;
        y += v.y;
        z += v.z;
        w += v.w;
        return *this;
    }

    constexpr tvec4<T> operator-(T t) const { return { x - t, y - t, z - t, w - t }; }
    constexpr tvec4<T> operator-(const tvec4<T>& v) const { return { x - v.x, y - v.y, z - v.z, w - v.w }; }
    constexpr tvec4<T>& operator-=(const tvec4<T>& v)
    {
        x -= v.x;
        y -= v.y;
        z -= v.z;
        w -= v.w;
        return *this;
    }

    constexpr tvec4<T> operator*(const tvec4<T>& v) const { return { x * v.x, y * v.y, z * v.z, w * v.w }; }
    constexpr tvec4<T>& operator*=(const tvec4<T>& v)
    {
        x *= v.x;
        y *= v.y;
        z *= v.z;
        w *= v.w;
        return *this;
    }

    constexpr tvec4<T> operator/(const tvec4<T>& v) const { return { x / v.x, y / v.y, z / v.z, w / v.w }; }
    constexpr tvec4<T>& operator/=(const tvec4<T>& v)
    {
        x /= v.x;
        y /= v.y;
        z /= v.z;
        w /= v.w;
        return *this;
    }

    constexpr tvec4<T> operator*(T f) const { return { x * f, y * f, z * f, w * f }; }
    constexpr tvec4<T>& operator*=(T f)
    {
        x *= f;
        y *= f;
        z *= f;
        w *= f;
        return *this;
    }

    constexpr tvec4<T> operator/(T f) const { return { x / f, y / f, z / f, w / f }; }
    constexpr tvec4<T>& operator/=(T f)
    {
        x /= f;
        y /= f;
        z /= f;
        w /= f;
        return *this;
    }

    constexpr tvec4<bool> operator==(const tvec4<T>& v) const
    {
        return tvec4<bool>(x == v.x, y == v.y, z == v.z, w == v.w);
    }

    constexpr tvec4<bool> operator!=(const tvec4<T>& v) const
    {
        return tvec4<bool>(x != v.x, y != v.y, z != v.z, w != v.w);
    }

    // (a rare member function to simulate swizzling)
    constexpr tvec3<T> xyz() const
    {
        return { x, y, z };
    }
};

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr tvec4<T> operator*(T lhs, const tvec4<T>& rhs)
{
    return rhs * lhs;
}

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr T dot(const tvec4<T>& lhs, const tvec4<T>& rhs)
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z + lhs.w * rhs.w;
}

template<>
inline f32 dot(const tvec4<f32>& lhs, const tvec4<f32>& rhs)
{
#ifdef __SSE__
    auto* a = reinterpret_cast<const __m128*>(value_ptr(lhs));
    auto* b = reinterpret_cast<const __m128*>(value_ptr(rhs));
    __m128 prod = _mm_mul_ps(*a, *b);
    return prod[0] + prod[1] + prod[2] + prod[3];
#else
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z + lhs.w * rhs.w;
#endif
}

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr T dotVec4WithVec3ImplicitW1(const tvec4<T>& lhs, const tvec3<T>& rhs)
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z + lhs.w * static_cast<T>(1.0);
}

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr T length2(const tvec4<T>& v)
{
    return dot(v, v);
}

template<typename T, ENABLE_IF_FLOATING_POINT(T)>
constexpr T length(const tvec4<T>& v)
{
    return std::sqrt(length2(v));
}

template<typename T, ENABLE_IF_FLOATING_POINT(T)>
constexpr T distance(const tvec4<T>& a, const tvec4<T>& b)
{
    return length(a - b);
}

template<typename T, ENABLE_IF_FLOATING_POINT(T)>
constexpr tvec4<T> normalize(const tvec4<T>& v)
{
    return v / length(v);
}

template<typename T, ENABLE_IF_FLOATING_POINT(T)>
constexpr tvec4<T> lerp(const tvec4<T>& a, const tvec4<T>& b, T x)
{
    return (static_cast<T>(1) - x) * a + x * b;
}

using vec4 = tvec4<Float>;
using fvec4 = tvec4<f32>;
using dvec4 = tvec4<f64>;
using uvec4 = tvec4<u32>;
using ivec4 = tvec4<i32>;
using bvec4 = tvec4<bool>;

// Vector math constants

constexpr vec3 globalX = vec3(static_cast<Float>(1), static_cast<Float>(0), static_cast<Float>(0));
constexpr vec3 globalY = vec3(static_cast<Float>(0), static_cast<Float>(1), static_cast<Float>(0));
constexpr vec3 globalZ = vec3(static_cast<Float>(0), static_cast<Float>(0), static_cast<Float>(1));

// (NOTE: Using a y-up right-handed coordinate system)
constexpr vec3 globalRight = +globalX;
constexpr vec3 globalUp = +globalY;
constexpr vec3 globalForward = -globalZ;

} // namespace ark

#ifndef ARK_DONT_EXPOSE_COMMON_MATH_TYPES
using vec2 = ark::vec2;
using vec3 = ark::vec3;
using vec4 = ark::vec4;
using ivec2 = ark::ivec2;
using ivec3 = ark::ivec3;
using ivec4 = ark::ivec4;
using uvec2 = ark::uvec2;
using uvec3 = ark::uvec3;
using uvec4 = ark::uvec4;
#endif
