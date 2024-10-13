/*
 * MIT License
 *
 * Copyright (c) 2024 Simon Moos
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

#include <utility> // for std::move & std::forward

namespace ark {

// ark::UniquePtr <=> std::unique_ptr (but not guaranteed to be c++ standard compliant)

template<typename T>
class [[nodiscard]] UniquePtr {
public:
    constexpr explicit UniquePtr(T* ptr)
        : m_ptr(ptr)
    {
    }

    constexpr UniquePtr(std::nullptr_t)
        : m_ptr(nullptr)
    {
    }

    constexpr UniquePtr()
        : UniquePtr(nullptr)
    {
    }

    constexpr UniquePtr(UniquePtr&& other)
        : UniquePtr(nullptr)
    {
        swap(other);
    }

    constexpr ~UniquePtr()
    {
        if (m_ptr != nullptr) {
            delete m_ptr;
            m_ptr = nullptr;
        }
    }

    constexpr UniquePtr(UniquePtr const&) = delete;
    constexpr UniquePtr& operator=(UniquePtr const&) = delete;

    constexpr UniquePtr& operator=(UniquePtr&& other)
    {
        UniquePtr<T> tempObject { std::move(other) };
        swap(tempObject);
        return *this;
    }

    constexpr void swap(UniquePtr&& other)
    {
        T* tempPtr = m_ptr;
        m_ptr = other.m_ptr;
        other.m_ptr = tempPtr;
    }

    constexpr void reset(T* ptr)
    {
        UniquePtr<T> tempObject = UniquePtr<T>(ptr);
        swap(tempObject);
    }

    constexpr T* release()
    {
        T* ptr = m_ptr;
        m_ptr = nullptr;
        return ptr;
    }

    constexpr T* get() const
    {
        return m_ptr;
    }

    constexpr T* operator->()
    {
        return m_ptr;
    }

    constexpr T& operator*()
    {
        return *m_ptr;
    }

    constexpr explicit operator bool() const
    {
        return m_ptr != nullptr;
    }

    constexpr bool operator==(UniquePtr const&) const = default;
    constexpr auto operator<=>(UniquePtr const&) const = default;

private:
    T* m_ptr { nullptr };
};

template<class T, class... Args>
UniquePtr<T> MakeUnique(Args&&... args)
{
    return UniquePtr<T>(new T { std::forward<Args>(args)... });
}

} // namespace ark

#ifndef ARK_DONT_EXPOSE_COMMON_TYPES
using ark::UniquePtr;
using ark::MakeUnique;
#endif
