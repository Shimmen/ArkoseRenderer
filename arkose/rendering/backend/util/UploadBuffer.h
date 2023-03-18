#pragma once

#include "rendering/backend/base/Backend.h"
#include <variant>

struct BufferCopyOperation {
    size_t size;

    Buffer* srcBuffer;
    size_t srcOffset;

    struct BufferDestination {
        Buffer* buffer { nullptr };
        size_t offset { 0 };
    };

    struct TextureDestination {
        Texture* texture { nullptr };
        size_t textureMip { 0 };
        size_t textureArrayLayer { 0 };
    };

    std::variant<BufferDestination, TextureDestination> destination {};
};

class UploadBuffer final {
public:
    UploadBuffer(Backend&, size_t size);

    UploadBuffer(UploadBuffer&) = delete;
    UploadBuffer& operator=(UploadBuffer&) = delete;

    std::vector<BufferCopyOperation> popPendingOperations();
    const std::vector<BufferCopyOperation>& peekPendingOperations() const { return m_pendingOperations; }

    size_t size() const { return m_buffer->size(); }
    size_t remainingSize() const { return size() - m_cursor; }

    void reset();

    bool upload(const void* data, size_t size, Buffer& dstBuffer, size_t dstOffset = 0);
    bool upload(const void* data, size_t size, Texture& dstTexture, size_t dstTextureMip, size_t dstTextureArrayLayer = 0);

    template<typename T>
    bool upload(const T& object, Buffer& dstBuffer, size_t dstOffset = 0)
    {
        const void* data = reinterpret_cast<const void*>(&object);
        return upload(data, sizeof(T), dstBuffer, dstOffset);
    }

    template<typename T>
    bool upload(const std::vector<T>& data, Buffer& dstBuffer, size_t dstOffset = 0)
    {
        return upload(data.data(), sizeof(T) * data.size(), dstBuffer, dstOffset);
    }

    template<typename T>
    bool upload(const std::span<T>& span, Buffer& dstBuffer, size_t dstOffset = 0)
    {
        return upload(span.data(), sizeof(T) * span.size(), dstBuffer, dstOffset);
    }

private:

    bool upload(const void* data, size_t size, std::variant<BufferCopyOperation::BufferDestination, BufferCopyOperation::TextureDestination>&& destination);

    size_t m_cursor { 0 };
    std::vector<BufferCopyOperation> m_pendingOperations;
    std::unique_ptr<Buffer> m_buffer;
};
