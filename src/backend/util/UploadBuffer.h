#pragma once

#include "backend/base/Backend.h"

struct BufferCopyOperation {
    size_t size;

    Buffer* srcBuffer;
    size_t srcOffset;

    Buffer* dstBuffer;
    size_t dstOffset;
};

struct UploadBuffer final {
    UploadBuffer(Backend&, size_t size);

    UploadBuffer(UploadBuffer&) = delete;
    UploadBuffer& operator=(UploadBuffer&) = delete;

    std::vector<BufferCopyOperation> popPendingOperations();
    const std::vector<BufferCopyOperation>& peekPendingOperations() const { return m_pendingOperations; }

    void reset();

    BufferCopyOperation upload(const void* data, size_t size, Buffer& dstBuffer);

    template<typename T>
    BufferCopyOperation upload(const T& object, Buffer& dstBuffer)
    {
        const void* data = reinterpret_cast<const void*>(&object);
        return upload(data, sizeof(T), dstBuffer);
    }

    template<typename T>
    BufferCopyOperation upload(const std::vector<T>& data, Buffer& dstBuffer)
    {
        return upload(data.data(), sizeof(T) * data.size(), dstBuffer);
    }

private:
    size_t m_cursor { 0 };
    std::vector<BufferCopyOperation> m_pendingOperations;
    std::unique_ptr<Buffer> m_buffer;
};
