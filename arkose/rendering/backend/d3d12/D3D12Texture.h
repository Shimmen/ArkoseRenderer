#pragma once

#include "rendering/backend/base/Texture.h"

#include "rendering/backend/d3d12/D3D12Common.h"

struct D3D12Texture final : public Texture {
public:
    D3D12Texture() = default;
    D3D12Texture(Backend&, Description);
    virtual ~D3D12Texture() override;

    virtual void setName(const std::string& name) override;

    void clear(ClearColor) override;
    void setData(const void* data, size_t size, size_t mipIdx, size_t arrayIdx) override;

    void generateMipmaps() override;

    ImTextureID asImTextureID() override;

    ComPtr<ID3D12Resource> textureResource;
    D3D12_RESOURCE_STATES resourceState { D3D12_RESOURCE_STATE_COMMON };

    D3D12_RESOURCE_DESC textureDescription {};
    DXGI_FORMAT dxgiFormat { DXGI_FORMAT_UNKNOWN };

    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> srvNoAlphaForImGui {};
};
