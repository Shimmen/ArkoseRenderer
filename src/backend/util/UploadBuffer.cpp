#include "UploadBuffer.h"

#include "utility/Logging.h"

UploadBuffer::UploadBuffer(Backend& backend, size_t size)
{
    // TODO: Maybe create a persistent mapping for this buffer? Makes sense considering its use.
    m_buffer = backend.createBuffer(size, Buffer::Usage::Transfer, Buffer::MemoryHint::TransferOptimal);
}

void UploadBuffer::reset()
{
    m_cursor = 0;
    m_pendingOperations.clear();
}

BufferCopyOperation UploadBuffer::upload(const void* data, size_t size, Buffer& dstBuffer)
{
    size_t requiredSize = m_cursor + size;
    if (requiredSize > m_buffer->size())
        LogWarning("UploadBuffer: needs to grow to fit all requested uploads! It might be good to increase the default size so we don't have to pay this runtime cost");

    BufferCopyOperation copyOperation;
    copyOperation.size = size;

    copyOperation.srcBuffer = m_buffer.get();
    copyOperation.srcOffset = m_cursor;

    copyOperation.dstBuffer = &dstBuffer;
    copyOperation.dstOffset = 0;

    m_buffer->updateDataAndGrowIfRequired(data, size, m_cursor);
    m_cursor += size;

    m_pendingOperations.push_back(copyOperation);
    return copyOperation;
}