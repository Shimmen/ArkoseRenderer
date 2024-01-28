#include "D3D12BindingSet.h"

#include "rendering/backend/d3d12/D3D12Backend.h"
#include "utility/Profiling.h"

D3D12BindingSet::D3D12BindingSet(Backend& backend, std::vector<ShaderBinding> bindings)
    : BindingSet(backend, std::move(bindings))
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    auto const& d3d12Backend = static_cast<D3D12Backend const&>(backend);
    ID3D12Device const& device = d3d12Backend.device();

    for (auto& bindingInfo : shaderBindings()) {

        D3D12_ROOT_PARAMETER parameter;

        switch (bindingInfo.type()) {
        case ShaderBindingType::ConstantBuffer:
            parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            parameter.Descriptor.ShaderRegister = UndecidedRegisterSlot;
            parameter.Descriptor.RegisterSpace = UndecidedRegisterSpace;
            break;
        case ShaderBindingType::StorageBuffer:
            parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
            parameter.Descriptor.ShaderRegister = UndecidedRegisterSlot;
            parameter.Descriptor.RegisterSpace = UndecidedRegisterSpace;
            break;
        case ShaderBindingType::StorageTexture:
            parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
            parameter.Descriptor.ShaderRegister = UndecidedRegisterSlot;
            parameter.Descriptor.RegisterSpace = UndecidedRegisterSpace;
            break;
        case ShaderBindingType::SampledTexture:
            parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
            parameter.Descriptor.ShaderRegister = UndecidedRegisterSlot;
            parameter.Descriptor.RegisterSpace = UndecidedRegisterSpace;
            break;
        case ShaderBindingType::RTAccelerationStructure:
            NOT_YET_IMPLEMENTED();
            break;
        default:
            ASSERT_NOT_REACHED();
        }

        switch (bindingInfo.shaderStage()) {
        case ShaderStage::Vertex:
            parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
            break;
        case ShaderStage::Fragment:
            parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            break;
        case ShaderStage::Task:
            parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_AMPLIFICATION;
            break;
        case ShaderStage::Mesh:
            parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_MESH;
            break;
        default:
            // No more fine grained options available, simply go with "all"
            parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        }

        rootParameters.push_back(parameter);
    }

    // The shader bindings is out frontend representation, while the root parameters are what our D3D12 backend actually uses.
    // To bind e.g. a buffer to root parameter 2 we need to look up shader binding 2 and ask for its buffer to bind it.
    // Therefore we need these to match 1:1.
    ARKOSE_ASSERT(shaderBindings().size() == rootParameters.size());

    // Q: Where are we actually creating the root signature for all these parameters?
    // A: The PSO wrapper (RenderState, ComputeState, RayTracingState) will create them!
}

D3D12BindingSet::~D3D12BindingSet()
{
    if (!hasBackend())
        return;
    // TODO
}

void D3D12BindingSet::setName(const std::string& name)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    Resource::setName(name);

    // TODO
}

void D3D12BindingSet::updateTextures(uint32_t bindingIndex, const std::vector<TextureBindingUpdate>& textureUpdates)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    // TODO
}
