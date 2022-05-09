#pragma once

#include "backend/base/Backend.h"

struct BufferCopyOperation {
    size_t size;

    Buffer* srcBuffer;
    size_t srcOffset;

    Buffer* dstBuffer;
    size_t dstOffset;
};

class UploadBuffer final {
public:
    UploadBuffer(Backend&, size_t size);

    UploadBuffer(UploadBuffer&) = delete;
    UploadBuffer& operator=(UploadBuffer&) = delete;

    std::vector<BufferCopyOperation> popPendingOperations();
    const std::vector<BufferCopyOperation>& peekPendingOperations() const { return m_pendingOperations; }

    void reset();

    BufferCopyOperation upload(const void* data, size_t size, Buffer& dstBuffer, size_t dstOffset = 0);

    template<typename T>
    BufferCopyOperation upload(const T& object, Buffer& dstBuffer, size_t dstOffset = 0)
    {
        const void* data = reinterpret_cast<const void*>(&object);
        return upload(data, sizeof(T), dstBuffer, dstOffset);
    }

    template<typename T>
    BufferCopyOperation upload(const std::vector<T>& data, Buffer& dstBuffer, size_t dstOffset = 0)
    {
        return upload(data.data(), sizeof(T) * data.size(), dstBuffer, dstOffset);
    }

private:
    size_t m_cursor { 0 };
    std::vector<BufferCopyOperation> m_pendingOperations;
    std::unique_ptr<Buffer> m_buffer;
};
