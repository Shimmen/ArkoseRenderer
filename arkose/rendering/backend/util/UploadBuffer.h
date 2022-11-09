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

    void reset();

    void upload(const void* data, size_t size, Buffer& dstBuffer, size_t dstOffset = 0);
    void upload(const void* data, size_t size, Texture& dstTexture, size_t dstTextureMip, size_t dstTextureArrayLayer = 0);

    template<typename T>
    void upload(const T& object, Buffer& dstBuffer, size_t dstOffset = 0)
    {
        const void* data = reinterpret_cast<const void*>(&object);
        upload(data, sizeof(T), dstBuffer, dstOffset);
    }

    template<typename T>
    void upload(const std::vector<T>& data, Buffer& dstBuffer, size_t dstOffset = 0)
    {
        upload(data.data(), sizeof(T) * data.size(), dstBuffer, dstOffset);
    }

private:

    void upload(const void* data, size_t size, std::variant<BufferCopyOperation::BufferDestination, BufferCopyOperation::TextureDestination>&& destination);

    size_t m_cursor { 0 };
    std::vector<BufferCopyOperation> m_pendingOperations;
    std::unique_ptr<Buffer> m_buffer;
};
