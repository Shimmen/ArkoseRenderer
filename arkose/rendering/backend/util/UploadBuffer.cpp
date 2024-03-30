#include "UploadBuffer.h"

#include "core/Logging.h"

UploadBuffer::UploadBuffer(Backend& backend, size_t size)
{
    // TODO: Maybe create a persistent mapping for this buffer? Makes sense considering its use.
    m_buffer = backend.createBuffer(size, Buffer::Usage::Upload);
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

bool UploadBuffer::upload(const void* data, size_t size, Buffer& dstBuffer, size_t dstOffset)
{
    return upload(data, size, BufferCopyOperation::BufferDestination { .buffer = &dstBuffer, .offset = dstOffset });
}

bool UploadBuffer::upload(const void* data, size_t size, Texture& dstTexture, size_t dstTextureMip, size_t dstTextureArrayLayer)
{
    return upload(data, size, BufferCopyOperation::TextureDestination { .texture = &dstTexture, .textureMip = dstTextureMip, .textureArrayLayer = dstTextureArrayLayer });
}

bool UploadBuffer::upload(const void* data, size_t size, std::variant<BufferCopyOperation::BufferDestination, BufferCopyOperation::TextureDestination>&& destination)
{
    if (std::holds_alternative<BufferCopyOperation::BufferDestination>(destination)) {
        auto const& copyDestination = std::get<BufferCopyOperation::BufferDestination>(destination).buffer;
        if (copyDestination->usage() == Buffer::Usage::Upload) {
            ARKOSE_LOG(Fatal, "Trying to use the upload buffer to upload to an upload buffer, which is not allowed, exiting.");
        }
    }

    size_t requiredSize = m_cursor + size;
    if (requiredSize > m_buffer->size()) {
        ARKOSE_LOG(Error, "UploadBuffer: not enough space for all requested uploads");
        return false;
    }

    BufferCopyOperation copyOperation;
    copyOperation.size = size;

    copyOperation.srcBuffer = m_buffer.get();
    copyOperation.srcOffset = m_cursor;

    copyOperation.destination = std::move(destination);

    m_buffer->updateData(data, size, m_cursor);
    m_cursor += size;

    m_pendingOperations.push_back(copyOperation);
    return true;
}
