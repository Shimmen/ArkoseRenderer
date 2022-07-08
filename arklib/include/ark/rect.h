/*
 * MIT License
 *
 * Copyright (c) 2022 Simon Moos
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

#include "vector.h"

namespace ark {

struct Rect2D {

    Rect2D() = default;

    Rect2D(ivec2 inOrigin, ivec2 inSize)
        : origin(inOrigin)
        , size(inSize)
    {
        ARK_ASSERT(all(greaterThanEqual(size, ivec2(0, 0))));
    }

    explicit Rect2D(ivec2 size)
        : Rect2D(ivec2(0, 0), size)
    {
    }

    [[nodiscard]] Rect2D inflated(int units) const
    {
        return Rect2D(origin - ivec2(units), max(size + ivec2(2 * units), ivec2(0, 0)));
    }

    [[nodiscard]] Rect2D deflated(int units) const
    {
        return inflated(-units);
    }

    bool subdivide(Rect2D& bl, Rect2D& br, Rect2D& tl, Rect2D& tr) const
    {
        if (any(lessThan(size, ivec2(2)))) {
            return false;
        }

        // NOTE: Integer division
        ivec2 quadrantSize = size / 2;

        bl = Rect2D(origin, quadrantSize);
        br = Rect2D(origin + ivec2(quadrantSize.x, 0), quadrantSize);
        tl = Rect2D(origin + ivec2(0, quadrantSize.y), quadrantSize);
        tr = Rect2D(origin + quadrantSize, quadrantSize);

        return true;
    }

    bool subdivideWithBorder(Rect2D& bl, Rect2D& br, Rect2D& tl, Rect2D& tr, u32 border) const
    {
        if (subdivide(bl, br, tl, tr) == false) {
            return false;
        }

        ARK_ASSERT(bl.size == br.size);
        ARK_ASSERT(tl.size == tr.size);
        ARK_ASSERT(bl.size == tl.size);

        // Each quadrant should have at least 1 unit size and room for border on all sides
        if (any(lessThan(bl.size, ivec2(1 + 2 * border)))) {
            return false;
        }

        bl = bl.deflated(border);
        br = br.deflated(border);
        tl = tl.deflated(border);
        tr = tr.deflated(border);

        return true;
    }

    ivec2 origin {};
    ivec2 size {};
};

} // namespace ark

#ifndef ARK_DONT_EXPOSE_COMMON_MATH_TYPES
using Rect2D = ark::Rect2D;
#endif
