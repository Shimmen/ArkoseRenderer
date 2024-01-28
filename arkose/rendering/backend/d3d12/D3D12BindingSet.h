#pragma once

#include "rendering/backend/base/BindingSet.h"

#include "rendering/backend/d3d12/D3D12Common.h"

struct D3D12BindingSet : public BindingSet {
public:
    D3D12BindingSet(Backend&, std::vector<ShaderBinding>);
    virtual ~D3D12BindingSet() override;

    virtual void setName(const std::string& name) override;
    virtual void updateTextures(uint32_t index, const std::vector<TextureBindingUpdate>&) override;

    //
    // NOTE: In Arkose we have binding sets which bind to a specific space/set, but this doesn't
    // map directly to how D3D12 works. For consistency we still have binding sets (well, it's
    // how Arkose works) but here we don't do much more than just set up the root parameters,
    // and we leave the binding slots undecided. These will be decided by the render/compute/rt
    // states when we create one with a binding set.
    // 
    // For the register space: if this binding set is bound at set index 0 it will get that space
    //
    // For the register slots: we'll have to count up the total registers used for the state and
    //  assign accordingly, so we don't get any overlap.
    //
    static constexpr u32 UndecidedRegisterSpace = 12345;
    static constexpr u32 UndecidedRegisterSlot = 67890;

    std::vector<D3D12_ROOT_PARAMETER> rootParameters {};
};
