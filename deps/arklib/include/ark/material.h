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

#include "color.h"
#include "vector.h"

namespace ark {

namespace material {

    // NOTE: Unless otherwise mentioned, all colors in this file are in gamma-encoded sRGB

    Float schlickFresnel(Float F0, Float theta)
    {
        Float p = 1.0 - std::cos(theta);
        return F0 + (1.0 - F0) * (p * p * p * p * p);
    }

    namespace fresnelF0 {

        constexpr vec3 commonDielectric = vec3(0.04);

        // From the Filament documentation: https://google.github.io/filament/Filament.md#toc4.8.3.2
        // TODO: Is this gamma-encoded sRGB? I would assume so, since they provide web colors next to it
        namespace metal {
            constexpr vec3 silver = { 0.97, 0.96, 0.91 };
            constexpr vec3 aluminium = { 0.91, 0.92, 0.92 };
            constexpr vec3 titanium = { 0.76, 0.73, 0.69 };
            constexpr vec3 iron = { 0.77, 0.78, 0.78 };
            constexpr vec3 platinum = { 0.83, 0.81, 0.78 };
            constexpr vec3 gold = { 1.00, 0.85, 0.57 };
            constexpr vec3 brass = { 0.98, 0.90, 0.59 };
            constexpr vec3 copper = { 0.97, 0.74, 0.62 };
        }

    }

} // namespace material

} // namespace ark
