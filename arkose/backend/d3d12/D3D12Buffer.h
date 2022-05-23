#pragma once

#include "backend/base/Buffer.h"

struct D3D12Buffer final : public Buffer {
public:
    D3D12Buffer(Backend&, size_t size, Usage, MemoryHint);
    virtual ~D3D12Buffer() override;

    virtual void setName(const std::string& name) override;

    void updateData(const std::byte* data, size_t size, size_t offset) override;
    void reallocateWithSize(size_t newSize, ReallocateStrategy) override;
};
