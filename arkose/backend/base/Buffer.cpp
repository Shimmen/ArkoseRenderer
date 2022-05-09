#include "Buffer.h"

Buffer::Buffer(Backend& backend, size_t size, Usage usage, MemoryHint memoryHint)
    : Resource(backend)
    , m_size(size)
    , m_usage(usage)
    , m_memoryHint(memoryHint)
{
}

bool Buffer::updateDataAndGrowIfRequired(const std::byte* data, size_t size, size_t offset)
{
    size_t requiredBufferSize = offset + size;

    bool didGrow = false;
    if (m_size < requiredBufferSize) {
        size_t newSize = std::max(2 * m_size, requiredBufferSize);
        reallocateWithSize(newSize, Buffer::ReallocateStrategy::CopyExistingData);
        didGrow = true;
    }

    updateData(data, size, offset);
    return didGrow;
}