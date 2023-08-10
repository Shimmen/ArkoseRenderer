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

#include "core.h"

#include <functional> // for std::hash (?)
#include <limits> // for std::numeric_limits

namespace ark {

template<typename TypeTag>
struct Handle {

    using IndexType = uint64_t;

    static constexpr IndexType InvalidIndex = std::numeric_limits<IndexType>::max();

    Handle() = default;
    explicit Handle(IndexType index)
        : m_index(index)
    {
    }

    template<typename HandleT>
    bool operator==(HandleT& other) const
    {
        return m_index == other.m_index;
    }

    IndexType index() const
    {
        return m_index;
    }

    template<typename T>
    T indexOfType() const
    {
        static_assert(std::numeric_limits<T>::max() <= std::numeric_limits<IndexType>::max());
        // TODO: Add this assertion back! Move ARKOSE_ASSERT to ARK_ASSERT in arklib?
        //ARKOSE_ASSERT(m_index <= static_cast<IndexType>(std::numeric_limits<T>::max()));
        return static_cast<T>(m_index);
    }

    bool valid() const
    {
        return m_index != InvalidIndex;
    }

private:

    IndexType m_index { InvalidIndex };

};

} // namespace ark

namespace std {
template<typename>
struct hash;
}

// Use this #define to define your own custom "strong typedef" handle
#define ARK_DEFINE_HANDLE_TYPE(HandleType)                                                    \
    struct HandleType : Handle<struct HandleType##TypeTag> { using ark::Handle::Handle; };    \
    namespace std {                                                                           \
        template<>                                                                            \
        struct hash<HandleType> {                                                             \
            std::size_t operator()(const HandleType& handle) const                            \
            {                                                                                 \
                return std::hash<HandleType::IndexType>()(handle.index());                    \
            }                                                                                 \
        };                                                                                    \
    }
