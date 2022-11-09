#include "UploadBuffer.h"

#include "core/Logging.h"

UploadBuffer::UploadBuffer(Backend& backend, size_t size)
{
    // TODO: Maybe create a persistent mapping for this buffer? Makes sense considering its use.
    m_buffer = backend.createBuffer(size, Buffer::Usage::Transfer, Buffer::MemoryHint::TransferOptimal);
}

std::vector<BufferCopyOperation> UploadBuffer::popPendingOperations()
{
    auto pending = std::move(m_pendingOperations);
    m_pendingOperations.clear();
    return pending;
}

void UploadBuffer::reset()
{
    m_cursor = 0;
    if (m_pendingOperations.size() > 0)
        ARKOSE_LOG(Fatal, "UploadBuffer: resetting although not all pending operations have been executed, exiting.");
}

void UploadBuffer::upload(const void* data, size_t size, Buffer& dstBuffer, size_t dstOffset)
{
    upload(data, size, BufferCopyOperation::BufferDestination { .buffer = &dstBuffer, .offset = dstOffset });
}

void UploadBuffer::upload(const void* data, size_t size, Texture& dstTexture, size_t dstTextureMip, size_t dstTextureArrayLayer)
{
    upload(data, size, BufferCopyOperation::TextureDestination { .texture = &dstTexture, .textureMip = dstTextureMip, .textureArrayLayer = dstTextureArrayLayer });
}

void UploadBuffer::upload(const void* data, size_t size, std::variant<BufferCopyOperation::BufferDestination, BufferCopyOperation::TextureDestination>&& destination)
{
    size_t requiredSize = m_cursor + size;
    if (requiredSize > m_buffer->size())
        ARKOSE_LOG(Warning, "UploadBuffer: needs to grow to fit all requested uploads! It might be good to increase the default size so we don't have to pay this runtime cost");

    BufferCopyOperation copyOperation;
    copyOperation.size = size;

    copyOperation.srcBuffer = m_buffer.get();
    copyOperation.srcOffset = m_cursor;

    copyOperation.destination = std::move(destination);

    m_buffer->updateDataAndGrowIfRequired(data, size, m_cursor);
    m_cursor += size;

    m_pendingOperations.push_back(copyOperation);
}
