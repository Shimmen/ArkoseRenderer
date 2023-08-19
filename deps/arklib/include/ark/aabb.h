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
#include "vector.h"

#include <limits> // for std::numeric_limits etc.

namespace ark {

struct aabb3 {
    vec3 min;
    vec3 max;

    explicit aabb3(vec3 min = vec3(std::numeric_limits<Float>::infinity()), vec3 max = vec3(-std::numeric_limits<Float>::infinity()))
        : min(min)
        , max(max)
    {
    }

    vec3 extents() const
    {
        return max - min;
    }

    aabb3& expandWithPoint(const vec3& point)
    {
        min = ark::min(point, min);
        max = ark::max(point, max);
        return *this;
    }

    aabb3 transformed(mat4 transform) const
    {
        const vec3 transformedCorners[8] = { transform * vec3(min.x, min.y, min.z),
                                             transform * vec3(min.x, min.y, max.z),
                                             transform * vec3(min.x, max.y, min.z),
                                             transform * vec3(min.x, max.y, max.z),
                                             transform * vec3(max.x, min.y, min.z),
                                             transform * vec3(max.x, min.y, max.z),
                                             transform * vec3(max.x, max.y, min.z),
                                             transform * vec3(max.x, max.y, max.z) };

        aabb3 result {};

        for (int i = 0; i < 8; ++i) {
            result.min = ark::min(result.min, transformedCorners[i]);
            result.max = ark::max(result.max, transformedCorners[i]);
        }

        return result;
    }

    bool contains(const vec3& point) const
    {
        return all(greaterThanEqual(point, min) && lessThanEqual(point, max));
    }
};

} // namespace ark
