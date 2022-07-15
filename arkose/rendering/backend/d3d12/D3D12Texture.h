#pragma once

#include "rendering/backend/base/Texture.h"

struct D3D12Texture final : public Texture {
public:
    D3D12Texture(Backend&, Description);
    virtual ~D3D12Texture() override;

    virtual void setName(const std::string& name) override;

    void clear(ClearColor) override;

    void setPixelData(vec4 pixel) override;
    void setData(const void* data, size_t size) override;

    void generateMipmaps() override;
};
