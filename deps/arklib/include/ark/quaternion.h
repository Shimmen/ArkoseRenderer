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
#include "matrix.h"
#include "vector.h"

namespace ark {

template<typename T, typename _ = void>
struct tquat {
};

template<typename T>
struct tquat<T, ENABLE_STRUCT_IF_FLOATING_POINT(T)> {
    tvec3<T> vec;
    T w;

    constexpr tquat(tvec3<T> vec, T w) noexcept
        : vec(vec)
        , w(w)
    {
    }

    constexpr tquat() noexcept
        : tquat({ static_cast<T>(0), static_cast<T>(0), static_cast<T>(0) }, static_cast<T>(1))
    {
    }

    constexpr tquat<T> operator*(const tquat<T>& q) const
    {
        const tquat<T>& p = *this;
        return {
            p.w * q.vec + q.w * p.vec + cross(p.vec, q.vec),
            p.w * q.w - dot(p.vec, q.vec)
        };
    }

    constexpr tquat<T>& operator*=(const tquat<T>& q)
    {
        *this = *this * q;
        return *this;
    }

    constexpr tvec3<T> operator*(const tvec3<T>& v) const
    {
        // Method by Fabian 'ryg' Giessen who posted it on some now defunct forum. There is some info
        // at https://blog.molecular-matters.com/2013/05/24/a-faster-quaternion-vector-multiplication/.

        tvec3<T> t = static_cast<T>(2) * cross(vec, v);
        tvec3<T> res = v + w * t + cross(vec, t);

        return res;
    }

    constexpr bool operator==(const tquat<T>& q) const
    {
        return all(vec == q.vec) && w == q.w;
    }

    constexpr bool operator!=(const tquat<T>& q) const
    {
        return !all(vec == q.vec) || w != q.w;
    }

    constexpr bool isNormalized(T epsilon = static_cast<T>(0.0001)) const
    {
        return std::abs(length2(*this) - static_cast<T>(1)) < epsilon;
    }
};

template<typename T, ENABLE_IF_FLOATING_POINT(T)>
constexpr tquat<T> quatFromMatrix(const tmat4<T>& m)
{
    // This function is a rewritten version of Mike Day's "Converting a Rotation Matrix to a Quaternion" code. Probably not official,
    // but a copy of the document can be found at https://d3cw3dd2w32x2b.cloudfront.net/wp-content/uploads/2015/01/matrix-to-quat.pdf

    const T& m00 = m.x.x;
    const T& m11 = m.y.y;
    const T& m22 = m.z.z;

    tquat<T> q;
    T t = static_cast<T>(1);

    if (m22 < static_cast<T>(0)) {
        if (m00 > m11) {
            t += +m00 - m11 - m22;
            q = { { t, m.x.y + m.y.x, m.z.x + m.x.z }, m.y.z - m.z.y };
        } else {
            t += -m00 + m11 - m22;
            q = { { m.x.y + m.y.x, t, m.y.z + m.z.y }, m.z.x - m.x.z };
        }
    } else {
        if (m00 < -m11) {
            t += -m00 - m11 + m22;
            q = { { m.z.x + m.x.z, m.y.z + m.z.y, t }, m.x.y - m.y.x };
        } else {
            t += +m00 + m11 + m22;
            q = { { m.y.z - m.z.y, m.z.x - m.x.z, m.x.y - m.y.x }, t };
        }
    }

    T scale = static_cast<T>(0.5) / std::sqrt(t);
    q.vec *= scale;
    q.w *= scale;

    return q;
}

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr T length2(const tquat<T>& q)
{
    return ark::length2(q.vec) + ark::square(q.w);
}

template<typename T, ENABLE_IF_FLOATING_POINT(T)>
constexpr T length(const tquat<T>& q)
{
    return std::sqrt(length2(q));
}

template<typename T, ENABLE_IF_FLOATING_POINT(T)>
constexpr tquat<T> normalize(const tquat<T>& q)
{
    tquat<T> result;

    T len = length(q);
    if (len > static_cast<T>(0)) {
        result.vec = q.vec / len;
        result.w = q.w / len;
    }

    return result;
}

template<typename T, ENABLE_IF_ARITHMETIC(T)>
constexpr tquat<T> conjugate(const tquat<T>& q)
{
    return tquat<T>(-q.vec, q.w);
}

template<typename T, ENABLE_IF_FLOATING_POINT(T)>
constexpr tquat<T> inverse(const tquat<T>& q)
{
    T denominator = ark::length2(q);
    return tquat<T>(q.vec / denominator, q.w / denominator);
}

template<typename T, ENABLE_IF_FLOATING_POINT(T)>
constexpr tquat<T> axisAngle(const tvec3<T>& axis, T angle)
{
    T halfAngle = angle / static_cast<T>(2);
    tvec3<T> xyz = axis * std::sin(halfAngle);
    T w = std::cos(halfAngle);
    return tquat<T>(xyz, w);
}

template<typename T, ENABLE_IF_FLOATING_POINT(T)>
tquat<T> lookRotation(const tvec3<T>& forward, const tvec3<T>& tempUp)
{
    tvec3<T> right = cross(forward, tempUp);
    tvec3<T> up = cross(right, forward);
    mat3 orientationMat = mat3(right, up, -forward);
    return quatFromMatrix(mat4(orientationMat));
}

template<typename T, ENABLE_IF_FLOATING_POINT(T)>
constexpr tvec3<T> rotateVector(const tquat<T>& q, const tvec3<T>& v)
{
    return q * v;
}

template<typename T, ENABLE_IF_FLOATING_POINT(T)>
constexpr tvec3<T> quatToEulerAngles(const tquat<T>& q)
{
    // Rewritten version of https://en.wikipedia.org/wiki/Conversion_between_quaternions_and_Euler_angles#Quaternion_to_Euler_Angles_Conversion

    tvec3<T> euler;

    // Roll (x-axis rotation)
    T sinRollCosPitch = static_cast<T>(2) * (q.w * q.vec.x + q.vec.y * q.vec.z);
    T cosRollCosPitch = static_cast<T>(1) - static_cast<T>(2) * square(q.vec.x) + square(q.vec.y);
    euler.x = std::atan2(sinRollCosPitch, cosRollCosPitch);

    // Pitch (y-axis rotation)
    T sinPitch = static_cast<T>(2) * (q.w * q.vec.y - q.vec.z * q.vec.x);
    if (std::abs(sinPitch) >= static_cast<T>(1)) {
        euler.y = std::copysign(HALF_PI, sinPitch); // (clamp to +-90 degrees)
    } else {
        euler.y = std::asin(sinPitch);
    }

    // Yaw (z-axis rotation)
    T sinYawCosPitch = static_cast<T>(2) * (q.w * q.vec.z + q.vec.x * q.vec.y);
    T cosYawCosPitch = static_cast<T>(1) - static_cast<T>(2) * square(q.vec.y) + square(q.vec.z);
    euler.z = std::atan2(sinYawCosPitch, cosYawCosPitch);

    return euler;
}

template<typename T, ENABLE_IF_FLOATING_POINT(T)>
constexpr tquat<T> quatFromEulerAngles(const tvec3<T>& euler)
{
    // Rewritten version of https://en.wikipedia.org/wiki/Conversion_between_quaternions_and_Euler_angles#Euler_Angles_to_Quaternion_Conversion

    const T& roll = euler.x;
    const T& pitch = euler.y;
    const T& yaw = euler.z;

    T half = static_cast<T>(0.5);
    T cr = std::cos(roll * half);
    T sr = std::sin(roll * half);
    T cp = std::cos(pitch * half);
    T sp = std::sin(pitch * half);
    T cy = std::cos(yaw * half);
    T sy = std::sin(yaw * half);

    tquat<T> q;
    q.vec.x = sr * cp * cy - cr * sp * sy;
    q.vec.y = cr * sp * cy + sr * cp * sy;
    q.vec.z = cr * cp * sy - sr * sp * cy;
    q.w = cr * cp * cy + sr * sp * sy;

    return q;
}

template<typename T, ENABLE_IF_FLOATING_POINT(T)>
constexpr tmat3<T> quatToMatrix(const tquat<T>& q)
{
#if 1

    tmat3<T> res;
    res.x = rotateVector(q, globalX);
    res.y = rotateVector(q, globalY);
    res.z = rotateVector(q, globalZ);

#else

    // This function is a rewritten version of mat4x4_from_quat from https://github.com/datenwolf/linmath.h

    const T& a = q.w;
    const T& b = q.vec.x;
    const T& c = q.vec.y;
    const T& d = q.vec.z;
    T a2 = square(a);
    T b2 = square(b);
    T c2 = square(c);
    T d2 = square(d);

    tmat3<T> res;

    res.x.x = a2 + b2 - c2 - d2;
    res.x.y = static_cast<T>(2) * (b * c + a * d);
    res.x.z = static_cast<T>(2) * (b * d - a * c);

    res.y.x = static_cast<T>(2) * (b * c - a * d);
    res.y.y = a2 - b2 + c2 - d2;
    res.y.z = static_cast<T>(2) * (c * d + a * b);

    res.z.x = static_cast<T>(2) * (b * d + a * c);
    res.z.y = static_cast<T>(2) * (c * d - a * b);
    res.z.z = a2 - b2 - c2 + d2;

#endif

    return res;
}

using quat = tquat<Float>;
using fquat = tquat<f32>;
using dquat = tquat<f64>;

} // namespace ark

#ifndef ARK_DONT_EXPOSE_COMMON_MATH_TYPES
using quat = ark::quat;
#endif
