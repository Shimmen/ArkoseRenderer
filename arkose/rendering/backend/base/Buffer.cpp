#include "Buffer.h"

Buffer::Buffer(Backend& backend, size_t size, Usage usage)
    : Resource(backend)
    , m_size(size)
    , m_usage(usage)
{
}

bool Buffer::storageCapable() const
{
    switch (usage()) {
    case Buffer::Usage::Vertex:
    case Buffer::Usage::Index:
    case Buffer::Usage::StorageBuffer:
    case Buffer::Usage::IndirectBuffer:
    case Buffer::Usage::Readback: // assumed to be written to on the GPU
        return true;
    default:
        return false;
    }
}
