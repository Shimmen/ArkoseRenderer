#pragma once

#include "core/Types.h"
#include "rendering/backend/Resource.h"
#include <vector>

class Buffer : public Resource {
public:

    enum class Usage {
        Vertex,
        Index,
        RTInstanceBuffer,
        ConstantBuffer,
        StorageBuffer,
        IndirectBuffer,
        Upload,
        Readback,
    };

    Buffer() = default;
    Buffer(Backend&, size_t size, Usage usage);

    size_t size() const { return m_size; }
    Usage usage() const { return m_usage; }

    size_t stride() const { return m_stride; }
    bool hasStride() const { return m_stride != 0; }
    void setStride(size_t stride) { m_stride = stride; }

    size_t sizeInMemory() const { return m_sizeInMemory; }

    bool storageCapable() const;

    enum class MapMode {
        Read,
        Write,
        ReadWrite,
    };

    virtual bool mapData(MapMode, size_t size, size_t offset, std::function<void(std::byte*)>&& mapCallback) = 0;

    virtual void updateData(const std::byte* data, size_t size, size_t offset = 0) = 0;

    template<typename T>
    void updateData(const T* data, size_t size, size_t offset = 0)
    {
        auto* byteData = reinterpret_cast<const std::byte*>(data);
        updateData(byteData, size, offset);
    }

    template<typename T>
    void updateData(const std::vector<T>& vector, size_t offset = 0)
    {
        auto* byteData = reinterpret_cast<const std::byte*>(vector.data());
        size_t byteSize = vector.size() * sizeof(T);
        updateData(byteData, byteSize, offset);
    }

    enum class ReallocateStrategy {
        CopyExistingData,
        DiscardExistingData,
    };

    virtual void reallocateWithSize(size_t newSize, ReallocateStrategy) = 0;

protected:
    size_t m_size { 0 };
    size_t m_sizeInMemory { SIZE_MAX };

private:
    Usage m_usage { Usage::Vertex };
    size_t m_stride { 0 };
};
