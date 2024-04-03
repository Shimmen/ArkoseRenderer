#include "D3D12BindingSet.h"

#include "rendering/backend/d3d12/D3D12Backend.h"
#include "rendering/backend/d3d12/D3D12Buffer.h"
#include "utility/Profiling.h"

D3D12BindingSet::D3D12BindingSet(Backend& backend, std::vector<ShaderBinding> bindings)
    : BindingSet(backend, std::move(bindings))
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    auto& d3d12Backend = static_cast<D3D12Backend&>(backend);

    // TODO: Consider writing the descriptor directly in the root parameter if it's small enough (according to some heuristic)
    //       For example, a single buffer or texture could be written directly, and we then avoid one level of indirection.

    // Allocate descriptors for the descriptor table that this binding set constitutes
    {
        u32 totalDescriptorCount = 0;
        for (auto& bindingInfo : shaderBindings()) {
            totalDescriptorCount += bindingInfo.arrayCount();
        }

        ARKOSE_ASSERT(totalDescriptorCount > 0);
        descriptorTableAllocation = d3d12Backend.shaderVisibleDescriptorHeapAllocator().allocate(totalDescriptorCount);
    }

    // Setup descriptors & matching descriptor ranges for each shader binding
    {
        u32 currentDescriptorOffset = 0;

        for (auto& bindingInfo : shaderBindings()) {

            D3D12_DESCRIPTOR_RANGE& descriptorRange = descriptorRanges.emplace_back();
            descriptorRange.NumDescriptors = bindingInfo.arrayCount();
            descriptorRange.BaseShaderRegister = bindingInfo.bindingIndex();
            descriptorRange.RegisterSpace = UndecidedRegisterSpace; // To be resolved when making the PSO
            descriptorRange.OffsetInDescriptorsFromTableStart = currentDescriptorOffset;

            switch (bindingInfo.type()) {
            case ShaderBindingType::ConstantBuffer: {
                auto const& d3d12Buffer = static_cast<D3D12Buffer const&>(bindingInfo.getBuffer());

                D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc {};
                cbvDesc.BufferLocation = d3d12Buffer.bufferResource->GetGPUVirtualAddress();
                cbvDesc.SizeInBytes = d3d12Buffer.sizeInMemory();

                D3D12_CPU_DESCRIPTOR_HANDLE descriptor = descriptorTableAllocation.cpuDescriptorAt(currentDescriptorOffset++);
                d3d12Backend.device().CreateConstantBufferView(&cbvDesc, descriptor);

                descriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;

            } break;
            case ShaderBindingType::StorageBuffer: {

                ARKOSE_ASSERT(bindingInfo.arrayCount() == bindingInfo.getBuffers().size());
                if (bindingInfo.arrayCount() == 0) {
                    continue;
                }

                for (u32 idx = 0; idx < bindingInfo.arrayCount(); ++idx) {

                    Buffer const* buffer = bindingInfo.getBuffers()[idx];
                    ARKOSE_ASSERT(buffer);
                    auto& d3d12Buffer = static_cast<D3D12Buffer const&>(*buffer);

                    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc {};
                    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
                    uavDesc.Buffer.FirstElement = 0; // no offset (for now)
                    uavDesc.Buffer.CounterOffsetInBytes = 0; // not supported

                    // If StructureByteStride value is not 0, a view of a structured buffer is created and the D3D12_UNORDERED_ACCESS_VIEW_DESC::Format field
                    // must be DXGI_FORMAT_UNKNOWN. If StructureByteStride is 0, a typed view of a buffer is created and a format must be supplied.
                    // Alternatively, if `D3D12_BUFFER_UAV_FLAG_RAW` is passed along one can use `DXGI_FORMAT_R32_TYPELESS` for a raw buffer.

                    if (d3d12Buffer.hasStride()) {
                        // Create structured buffer
                        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
                        uavDesc.Buffer.NumElements = narrow_cast<u32>(d3d12Buffer.size() / d3d12Buffer.stride());
                        uavDesc.Buffer.StructureByteStride = narrow_cast<u32>(d3d12Buffer.stride());
                    } else {
                        // No stride available, create a raw (byte address) buffer
                        ASSERT_NOT_REACHED();
                        // NOTE: Currently I don't think we ever hit this code as we require storage buffers to have a stride
                        // so that they can act as structured buffer in D3D12. However, I want to maybe allow raw buffers layer..
                        uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
                        uavDesc.Buffer.NumElements = d3d12Buffer.size() / sizeof(u32);
                        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
                    }

                    D3D12_CPU_DESCRIPTOR_HANDLE descriptor = descriptorTableAllocation.cpuDescriptorAt(currentDescriptorOffset++);
                    d3d12Backend.device().CreateUnorderedAccessView(d3d12Buffer.bufferResource.Get(), nullptr, &uavDesc, descriptor);
                }

                descriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;

            } break;
            case ShaderBindingType::StorageTexture: {

                auto& d3d12Texture = static_cast<D3D12Texture const&>(bindingInfo.getStorageTexture().texture());
                u32 mipLevel = bindingInfo.getStorageTexture().mipLevel();

                D3D12_CPU_DESCRIPTOR_HANDLE descriptor = descriptorTableAllocation.cpuDescriptorAt(currentDescriptorOffset++);

                if (mipLevel == 0) {
                    // All textures have an image view for mip0 already available if it's storage/UAV capable
                    ARKOSE_ASSERT(d3d12Texture.uavDescriptor.valid());
                    d3d12Backend.device().CopyDescriptorsSimple(1,
                                                                descriptor,
                                                                d3d12Texture.uavDescriptor.firstCpuDescriptor,
                                                                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                } else {
                    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc {};
                    uavDesc.Format = d3d12Texture.dxgiFormat;
                    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

                    uavDesc.Texture2D.MipSlice = mipLevel;
                    uavDesc.Texture2D.PlaneSlice = 0;

                    d3d12Backend.device().CreateUnorderedAccessView(d3d12Texture.textureResource.Get(), nullptr, &uavDesc, descriptor); // allocation.firstCpuDescriptor);
                }

                descriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;

            } break;
            case ShaderBindingType::SampledTexture: {

                const auto& sampledTextures = bindingInfo.getSampledTextures();
                size_t numTextures = sampledTextures.size();
                ARKOSE_ASSERT(numTextures > 0);

                for (u32 idx = 0; idx < bindingInfo.arrayCount(); ++idx) {

                    // NOTE: Since we assume resource binding tier 3 we're actually allowed to leave a descriptor range partially unbound.
                    // However, I think it makes more sense to write some kind of "default" for all slots like this. Ideally write some
                    // special texture, e.g. magenta, but this will work for now (and is the same as we do for Vulkan).
                    const Texture* texture = (idx >= numTextures) ? sampledTextures.front() : sampledTextures[idx];
                    ARKOSE_ASSERT(texture);
                    auto& d3d12Texture = static_cast<D3D12Texture const&>(*texture);

                    D3D12_CPU_DESCRIPTOR_HANDLE descriptor = descriptorTableAllocation.cpuDescriptorAt(currentDescriptorOffset++);
                    d3d12Backend.device().CopyDescriptorsSimple(1,
                                                                descriptor,
                                                                d3d12Texture.srvDescriptor.firstCpuDescriptor,
                                                                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

                    // For now we only have combined image+sampler so always create a static sampler for each sampled image
                    // We assume that it will have the same register number as the texture itself (but with the sampler prefix).
                    D3D12_STATIC_SAMPLER_DESC staticSampler = d3d12Texture.createStaticSamplerDesc();
                    staticSampler.ShaderRegister = bindingInfo.bindingIndex();
                    staticSampler.RegisterSpace = UndecidedRegisterSpace; // to be resolved when making the PSO
                    staticSampler.ShaderVisibility = shaderVisibilityFromShaderStage(bindingInfo.shaderStage());
                    staticSamplers.push_back(staticSampler);
                }

                descriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;

            } break;
            case ShaderBindingType::RTAccelerationStructure: {
                NOT_YET_IMPLEMENTED();
            } break;
            default:
                ASSERT_NOT_REACHED();
            }
        }
    }

    // Define the root parameter for this descriptor table / binding set
    {
        std::optional<D3D12_SHADER_VISIBILITY> parameterVisibility {};
        for (auto& bindingInfo : shaderBindings()) {

            D3D12_SHADER_VISIBILITY visibilityForBinding = shaderVisibilityFromShaderStage(bindingInfo.shaderStage());

            if (parameterVisibility.has_value()) {
                if (parameterVisibility.value() != visibilityForBinding) {
                    // There are bindings with different visibilities within this set so we have to make it visible for all stages.
                    // There may be some slightly smarter configurations for this but this will have to work for now.
                    parameterVisibility = D3D12_SHADER_VISIBILITY_ALL;
                }
            } else {
                parameterVisibility = visibilityForBinding;
            }
        }

        rootParameter = {};
        rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameter.DescriptorTable.NumDescriptorRanges = narrow_cast<u32>(descriptorRanges.size());
        rootParameter.DescriptorTable.pDescriptorRanges = descriptorRanges.data();
        rootParameter.ShaderVisibility = parameterVisibility.value_or(D3D12_SHADER_VISIBILITY_ALL);

        // Q: Where are we actually creating the root signature for this and other root parameters?
        // A: The PSO wrapper (RenderState, ComputeState, RayTracingState) will create them!
    }
}

D3D12BindingSet::~D3D12BindingSet()
{
    if (!hasBackend()) {
        return;
    }

    auto& d3d12Backend = static_cast<D3D12Backend&>(backend());

    d3d12Backend.shaderVisibleDescriptorHeapAllocator().free(descriptorTableAllocation);
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

D3D12_SHADER_VISIBILITY D3D12BindingSet::shaderVisibilityFromShaderStage(ShaderStage shaderStage) const
{
    switch (shaderStage) {
    case ShaderStage::Vertex:
        return D3D12_SHADER_VISIBILITY_VERTEX;
        break;
    case ShaderStage::Fragment:
        return D3D12_SHADER_VISIBILITY_PIXEL;
        break;
    case ShaderStage::Compute:
        return D3D12_SHADER_VISIBILITY_ALL;
        break;
    case ShaderStage::Task:
        return D3D12_SHADER_VISIBILITY_AMPLIFICATION;
        break;
    case ShaderStage::Mesh:
        return D3D12_SHADER_VISIBILITY_MESH;
        break;
    default:
        // No more fine grained options available, simply go with "all"
        return D3D12_SHADER_VISIBILITY_ALL;
    }
}
