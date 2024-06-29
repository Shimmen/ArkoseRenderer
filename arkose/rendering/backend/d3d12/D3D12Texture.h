#pragma once

#include "rendering/backend/base/Texture.h"

#include "rendering/backend/d3d12/D3D12Common.h"
#include "rendering/backend/d3d12/D3D12DescriptorHeapAllocator.h"
#include <D3D12MemAlloc.h>

struct D3D12Texture final : public Texture {
public:
    D3D12Texture() = default;
    D3D12Texture(Backend&, Description);
    virtual ~D3D12Texture() override;

    virtual void setName(const std::string& name) override;

    virtual bool storageCapable() const override;

    void clear(ClearColor) override;
    void setData(const void* data, size_t size, size_t mipIdx, size_t arrayIdx) override;

    void generateMipmaps() override;

    ImTextureID asImTextureID() override;

    D3D12_SAMPLER_DESC createSamplerDesc() const;
    D3D12_STATIC_SAMPLER_DESC createStaticSamplerDesc() const;

    ComPtr<D3D12MA::Allocation> textureAllocation {};
    ComPtr<ID3D12Resource> textureResource;
    mutable D3D12_RESOURCE_STATES resourceState { D3D12_RESOURCE_STATE_COMMON };

    D3D12_RESOURCE_DESC textureDescription {};
    DXGI_FORMAT dxgiFormat { DXGI_FORMAT_UNKNOWN };

    D3D12DescriptorAllocation srvDescriptor {};
    D3D12DescriptorAllocation uavDescriptor {};

    D3D12DescriptorAllocation srvNoAlphaDesciptorForImGui {};

    D3D12DescriptorAllocation samplerDescriptor {};
};
