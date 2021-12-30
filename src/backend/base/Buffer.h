#pragma once

#include "backend/Resource.h"

class Buffer : public Resource {
public:

    enum class Usage {
        Vertex,
        Index,
        UniformBuffer,
        StorageBuffer,
        IndirectBuffer,
        Transfer,
    };

    enum class MemoryHint {
        TransferOptimal,
        GpuOptimal,
        GpuOnly,
        Readback,
    };

    Buffer() = default;
    Buffer(Backend&, size_t size, Usage usage, MemoryHint);

    size_t size() const { return m_size; }
    Usage usage() const { return m_usage; }
    MemoryHint memoryHint() const { return m_memoryHint; }

    virtual void updateData(const std::byte* data, size_t size, size_t offset = 0) = 0;
    virtual bool updateDataAndGrowIfRequired(const std::byte* data, size_t size, size_t offset);

    template<typename T>
    void updateData(const T* data, size_t size, size_t offset = 0)
    {
        auto* byteData = reinterpret_cast<const std::byte*>(data);
        updateData(byteData, size, offset);
    }

    template<typename T>
    bool updateDataAndGrowIfRequired(const T* data, size_t size, size_t offset = 0)
    {
        auto* byteData = reinterpret_cast<const std::byte*>(data);
        return updateDataAndGrowIfRequired(byteData, size, offset);
    }

    enum class ReallocateStrategy {
        CopyExistingData,
        DiscardExistingData,
    };

    virtual void reallocateWithSize(size_t newSize, ReallocateStrategy) = 0;

protected:
    size_t m_size { 0 };

private:
    Usage m_usage { Usage::Vertex };
    MemoryHint m_memoryHint { MemoryHint::GpuOptimal };
};
