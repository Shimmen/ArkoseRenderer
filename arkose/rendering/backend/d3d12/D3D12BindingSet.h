#pragma once

#include "rendering/backend/base/BindingSet.h"

#include "rendering/backend/d3d12/D3D12Common.h"
#include "rendering/backend/d3d12/D3D12DescriptorHeapAllocator.h"

struct D3D12BindingSet : public BindingSet {
public:
    D3D12BindingSet(Backend&, std::vector<ShaderBinding>);
    virtual ~D3D12BindingSet() override;

    virtual void setName(const std::string& name) override;
    virtual void updateTextures(uint32_t index, const std::vector<TextureBindingUpdate>&) override;

    D3D12_SHADER_VISIBILITY shaderVisibilityFromShaderStage(ShaderStage) const;

    // NOTE: We want to start filling out all the root parameter info when creating the binding set,
    // but in D3D12 this requires not just the binding slot/register but also the register space, i.e.,
    // the set index, which we don't have available yet. When filling out this info here we use this
    // special constant which indicates that it's yet to be decided and will have to be reassigned
    // when we create a render/compute/raytracing state which uses this binding set at a specific index.
    static constexpr u32 UndecidedRegisterSpace = UINT32_MAX;

    std::vector<D3D12_DESCRIPTOR_RANGE> descriptorRanges {};
    D3D12_ROOT_PARAMETER rootParameter;

    std::vector<D3D12_STATIC_SAMPLER_DESC> staticSamplers {};

    D3D12DescriptorAllocation descriptorTableAllocation {};
};
