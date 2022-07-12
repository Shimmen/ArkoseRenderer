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

#include "core.h"
#include "matrix.h"
#include "quaternion.h"

namespace ark {

template<typename T, ENABLE_IF_FLOATING_POINT(T)>
constexpr tmat4<T> scale(T s)
{
    tmat4<T> m(s);
    m.w.w = static_cast<T>(1);
    return m;
}

template<typename T, ENABLE_IF_FLOATING_POINT(T)>
constexpr tmat4<T> scale(const tvec3<T>& v)
{
    tmat4<T> m(static_cast<T>(1.0));
    m.x.x = v.x;
    m.y.y = v.y;
    m.z.z = v.z;
    return m;
}

template<typename T, ENABLE_IF_FLOATING_POINT(T)>
constexpr tmat4<T> translate(const tvec3<T>& v)
{
    tmat4<T> m(static_cast<T>(1.0));
    m.w = { v, static_cast<T>(1.0) };
    return m;
}

template<typename T, ENABLE_IF_FLOATING_POINT(T)>
constexpr tmat4<T> rotate(const tquat<T>& q)
{
    return quatToMatrix(q);
}

template<typename T, ENABLE_IF_FLOATING_POINT(T)>
constexpr void decomposeMatrixToTranslationRotationScale(tmat4<T> m, tvec3<T>& translation, tquat<T>& orientation, tvec3<T>& scale)
{
    // Extract translation and zero in matrix
    translation = m.w.xyz();
    m.w = vec4(0, 0, 0, 1);

    // Scale along each of the axes is the length of the column vectors
    scale = tvec3<T>(length(m.x), length(m.y), length(m.z));

    // Ortho-normalize the matrix
    m.x /= scale.x;
    m.y /= scale.y;
    m.z /= scale.z;

    // Assuming no shear, matrix is now the rotation part
    orientation = quatFromMatrix(m);
}

template<typename T, ENABLE_IF_FLOATING_POINT(T)>
constexpr tmat4<T> lookAt(const tvec3<T>& eye, const tvec3<T>& target, const tvec3<T>& tempUp = globalUp)
{
    tvec3<T> forward = normalize(target - eye);
    tvec3<T> right = normalize(cross(forward, tempUp));
    tvec3<T> up = cross(right, forward);

    // TODO(optimization): Maybe make a version which doesn't require transpose?
    tmat4<T> mTrans(
        { right, -dot(right, eye) },
        { up, -dot(up, eye) },
        { -forward, +dot(forward, eye) },
        { 0, 0, 0, 1 });
    tmat4<T> m = transpose(mTrans);
    return m;
}

// Projective transformations

template<typename T, ENABLE_IF_FLOATING_POINT(T)>
constexpr tmat4<T> perspectiveProjectionToVulkanClipSpace(T fovy, T aspectRatio, T zNear, T zFar)
{
    // Code rewritten from GLM: https://github.com/g-truc/glm/blob/master/glm/ext/matrix_clip_space.inl
    // Right-handed, depth in range [0, 1], and y-coordinates where the top is -1 and the bottom is +1.

    ARK_ASSERT(std::abs(aspectRatio - std::numeric_limits<T>::epsilon()) > static_cast<T>(0));
    ARK_ASSERT(std::abs(zFar - zNear) > std::numeric_limits<T>::epsilon());
    ARK_ASSERT(fovy > std::numeric_limits<T>::epsilon());

    T tanHalfFovy = std::tan(fovy / static_cast<T>(2));
    tmat4<T> m(static_cast<T>(0));

    m.x.x = static_cast<T>(1) / (aspectRatio * tanHalfFovy);
    m.y.y = -static_cast<T>(1) / (tanHalfFovy);
    m.z.z = zFar / (zNear - zFar);
    m.z.w = -static_cast<T>(1);
    m.w.z = -(zFar * zNear) / (zFar - zNear);

    return m;
}

template<typename T, ENABLE_IF_FLOATING_POINT(T)>
constexpr tmat4<T> perspectiveProjectionToOpenGLClipSpace(T fovy, T aspectRatio, T zNear, T zFar)
{
    // Code rewritten from GLM: https://github.com/g-truc/glm/blob/master/glm/ext/matrix_clip_space.inl
    // Right-handed, depth in range [-1, 1], and y-coordinates where the top is +1 and the bottom is -1.

    ARK_ASSERT(std::abs(aspectRatio - std::numeric_limits<T>::epsilon()) > static_cast<T>(0));
    ARK_ASSERT(std::abs(zFar - zNear) > std::numeric_limits<T>::epsilon());
    ARK_ASSERT(fovy > std::numeric_limits<T>::epsilon());

    T tanHalfFovy = std::tan(fovy / static_cast<T>(2));
    tmat4<T> m(static_cast<T>(0));

    m.x.x = static_cast<T>(1) / (aspectRatio * tanHalfFovy);
    m.y.y = static_cast<T>(1) / (tanHalfFovy);
    m.z.z = -(zFar + zNear) / (zFar - zNear);
    m.z.w = -static_cast<T>(1);
    m.w.z = -(static_cast<T>(2) * zFar * zNear) / (zFar - zNear);

    return m;
}

enum class OrthographicProjectionDepthMode {
    ZeroToOne,
    NegativeOneToOne
};

template<typename T, ENABLE_IF_FLOATING_POINT(T)>
constexpr tmat4<T> orthographicProjection(T left, T right, T bottom, T top, T zNear, T zFar, OrthographicProjectionDepthMode depthMode)
{
    // Code rewritten from GLM: https://github.com/g-truc/glm/blob/master/glm/ext/matrix_clip_space.inl
    // Right-handed with depth in range [0, 1].

    tmat4<T> m(static_cast<T>(1));

    m.x.x = static_cast<T>(2) / (right - left);
    m.y.y = static_cast<T>(2) / (top - bottom);
    m.w.x = -(right + left) / (right - left);
    m.w.y = -(top + bottom) / (top - bottom);

    switch (depthMode) {
    case OrthographicProjectionDepthMode::ZeroToOne:
        m.z.z = -static_cast<T>(1) / (zFar - zNear);
        m.w.z = -zNear / (zFar - zNear);
        break;
    case OrthographicProjectionDepthMode::NegativeOneToOne:
        m.z.z = -static_cast<T>(2) / (zFar - zNear);
        m.w.z = -(zFar + zNear) / (zFar - zNear);
        break;
    default:
        ARK_ASSERT(false);
    }

    return m;
}

template<typename T, ENABLE_IF_FLOATING_POINT(T)>
constexpr tmat4<T> orthographicProjectionToVulkanClipSpace(T size, T zNear, T zFar)
{
    T halfSize = size / static_cast<T>(2);
    return orthographicProjection(-halfSize, +halfSize, +halfSize, -halfSize, zNear, zFar, OrthographicProjectionDepthMode::ZeroToOne);
}

template<typename T, ENABLE_IF_FLOATING_POINT(T)>
constexpr tmat4<T> orthographicProjectionToOpenGLClipSpace(T size, T zNear, T zFar)
{
    T halfSize = size / static_cast<T>(2);
    return orthographicProjection(-halfSize, +halfSize, -halfSize, +halfSize, zNear, zFar, OrthographicProjectionDepthMode::NegativeOneToOne);
}

template<typename T, ENABLE_IF_FLOATING_POINT(T)>
constexpr void extractWorldFrustumPlanesFromViewProjection(const tmat4<T>& m, tvec4<T> planes[6])
{
    // Code rewritten from https://stackoverflow.com/a/34960913

    for (int i = 4; i--;) {
        planes[0][i] = m[i][3] + m[i][0]; // (left)
    }

    for (int i = 4; i--;) {
        planes[1][i] = m[i][3] - m[i][0]; // (right)
    }

    for (int i = 4; i--;) {
        planes[2][i] = m[i][3] + m[i][1]; // (bottom)
    }

    for (int i = 4; i--;) {
        planes[3][i] = m[i][3] - m[i][1]; // (top)
    }

    for (int i = 4; i--;) {
        planes[4][i] = m[i][3] + m[i][2]; // (near)
    }

    for (int i = 4; i--;) {
        planes[5][i] = m[i][3] - m[i][2]; // (far)
    }
}

} // namespace ark
