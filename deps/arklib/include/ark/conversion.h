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
#include <stddef.h> // for std::size_t

namespace ark {
namespace conversion {

namespace constants {
constexpr std::size_t BytesToKilobytes = (1 << 10);
constexpr std::size_t BytesToMegabytes = (1 << 20);
constexpr std::size_t BytesToGigabytes = (1 << 30);
}

// from bytes to ...
namespace to {
    template<typename InT, typename OutT = Float>
    constexpr OutT KB(InT bytes)
    {
        return static_cast<OutT>(bytes) / static_cast<OutT>(constants::BytesToKilobytes);
    }

    template<typename InT, typename OutT = Float>
    constexpr OutT MB(InT bytes)
    {
        return static_cast<OutT>(bytes) / static_cast<OutT>(constants::BytesToMegabytes);
    }

    template<typename InT, typename OutT = Float>
    constexpr OutT GB(InT bytes)
    {
        return static_cast<OutT>(bytes) / static_cast<OutT>(constants::BytesToGigabytes);
    }
}

}
}
