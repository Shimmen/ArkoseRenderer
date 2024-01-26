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

    void setPixelData(vec4 pixel) override;
    void setData(const void* data, size_t size, size_t mipIdx) override;

    void generateMipmaps() override;

    ImTextureID asImTextureID() override;

    ComPtr<ID3D12Resource> textureResource;
    D3D12_RESOURCE_STATES resourceState;

    DXGI_FORMAT dxgiFormat { DXGI_FORMAT_UNKNOWN };
};
