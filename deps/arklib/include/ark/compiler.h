/*
 * MIT License
 *
 * Copyright (c) 2023 Simon Moos
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

#if defined(_MSC_VER)
#define ARK_FORCE_INLINE __forceinline
#elif __has_attribute(always_inline)
#define ARK_FORCE_INLINE __attribute__((always_inline)) inline
#else
#warning "No implementation for ARK_FORCE_INLINE for this platform!"
#define ARK_FORCE_INLINE inline
#endif

#define ARK_DISABLE_OPTIMIZATIONS _Pragma("optimize(\"\", off)")
#define ARK_ENABLE_OPTIMIZATIONS _Pragma("optimize(\"\", on)")
