#pragma once

#include <memory>
#include <vector>

class BumpAllocator final {
public:

    explicit BumpAllocator(size_t size)
    {
        m_data.resize(size);
        reset();
    }

    template<typename T>
    [[nodiscard]] T* allocateAligned(size_t alignment = alignof(T));

    void reset()
    {
        m_cursor = m_data.data();
        m_remainingSize = m_data.size();
    }

private:

    std::vector<uint8_t> m_data {};

    void* m_cursor;
    size_t m_remainingSize;

};

template<typename T>
[[nodiscard]] T* BumpAllocator::allocateAligned(size_t alignment)
{
    if (std::align(alignment, sizeof(T), m_cursor, m_remainingSize)) {

        T* result = reinterpret_cast<T*>(m_cursor);

        m_cursor = static_cast<uint8_t*>(m_cursor) + sizeof(T);
        m_remainingSize -= sizeof(T);

        return result;

    }

    return nullptr;
}
