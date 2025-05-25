#pragma once

#include "rendering/backend/base/Sampler.h"

#include "rendering/backend/d3d12/D3D12Common.h"
#include "rendering/backend/d3d12/D3D12DescriptorHeapAllocator.h"

class Backend;

struct D3D12Sampler final : public Sampler {
public:
    D3D12Sampler() = default;
    D3D12Sampler(Backend&, Description&);
    virtual ~D3D12Sampler() override;

    D3D12DescriptorAllocation samplerDescriptor {};
};
