#pragma once

#include "rendering/backend/base/Buffer.h"
#include "rendering/backend/d3d12/D3D12Common.h"
#include <D3D12MemAlloc.h>

struct D3D12Buffer final : public Buffer {
public:
    D3D12Buffer(Backend&, size_t size, Usage);
    virtual ~D3D12Buffer() override;

    virtual void setName(const std::string& name) override;

    void updateData(const std::byte* data, size_t size, size_t offset) override;
    void reallocateWithSize(size_t newSize, ReallocateStrategy) override;

    ComPtr<D3D12MA::Allocation> bufferAllocation;
    ComPtr<ID3D12Resource> bufferResource;
    mutable D3D12_RESOURCE_STATES resourceState;
};
