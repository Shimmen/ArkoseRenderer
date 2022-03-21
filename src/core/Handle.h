#pragma once

#include "utility/util.h"
#include <limits>
#include <stdint.h>

template<typename TypeTag>
struct Handle {

    using IndexType = uint64_t;

    static constexpr IndexType InvalidIndex = std::numeric_limits<IndexType>::max();

    Handle() = default;
    Handle(IndexType index)
        : m_index(index)
    {
    }

    bool operator==(Handle& other) const
    {
        return index == other.index;
    }

    IndexType index() const
    {
        return m_index;
    }

    template<typename T>
    T indexOfType() const
    {
        static_assert(std::numeric_limits<T>::max() <= std::numeric_limits<IndexType>::max());
        ASSERT(m_index <= static_cast<IndexType>(std::numeric_limits<T>::max()));
        return static_cast<T>(m_index);
    }

    bool valid() const
    {
        return m_index != InvalidIndex;
    }

private:

    IndexType m_index { InvalidIndex };

};

namespace std {
template<typename>
struct hash;
}

// Use this #define to define your own custom "strong typedef" handle
#define DEFINE_HANDLE_TYPE(HandleType)                                                   \
    struct HandleType : Handle<struct HandleType##TypeTag> { using Handle::Handle; };    \
    namespace std {                                                                      \
        template<>                                                                       \
        struct hash<HandleType> {                                                        \
            std::size_t operator()(const HandleType& handle) const                       \
            {                                                                            \
                return std::hash<HandleType::IndexType>()(handle.index());               \
            }                                                                            \
        };                                                                               \
    }
