#pragma once

#include "core/Assert.h"
#include "core/Types.h"
#include <functional>
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
        ARKOSE_ASSERT(m_index <= static_cast<IndexType>(std::numeric_limits<T>::max()));
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
