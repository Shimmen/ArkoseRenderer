#pragma once

#include "rendering/backend/base/BindingSet.h"

#include "rendering/backend/d3d12/D3D12Common.h"

struct D3D12BindingSet : public BindingSet {
public:
    D3D12BindingSet(Backend&, std::vector<ShaderBinding>);
    virtual ~D3D12BindingSet() override;

    virtual void setName(const std::string& name) override;
    virtual void updateTextures(uint32_t index, const std::vector<TextureBindingUpdate>&) override;

    // NOTE: In Arkose we have binding sets which bind to a specific space/set, but D3D12 only has
    // a single root signature. We consider the root signature to be part of the render state, so
    // to make this work we leave the register space value in an undecided state, and later once
    // we create the render state we figure out what space it needs and creates it there and then.
    static constexpr u32 UndecidedRegisterSpaceValue = 12345;

    std::vector<D3D12_ROOT_PARAMETER> rootParameters {};
};
