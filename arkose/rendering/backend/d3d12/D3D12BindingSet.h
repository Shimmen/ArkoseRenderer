#pragma once

#include "rendering/backend/base/BindingSet.h"

struct D3D12BindingSet : public BindingSet {
public:
    D3D12BindingSet(Backend&, std::vector<ShaderBinding>);
    virtual ~D3D12BindingSet() override;

    virtual void setName(const std::string& name) override;
    virtual void updateTextures(uint32_t index, const std::vector<TextureBindingUpdate>&) override;
};
