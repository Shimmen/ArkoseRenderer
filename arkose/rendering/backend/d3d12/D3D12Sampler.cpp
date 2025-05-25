#include "D3D12Sampler.h"

#include "rendering/backend/d3d12/D3D12Backend.h"
#include "utility/Profiling.h"

D3D12Sampler::D3D12Sampler(Backend& backend, Description& desc)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    auto& d3d12Backend = static_cast<D3D12Backend&>(backend);

    D3D12_SAMPLER_DESC samplerDesc {};

    if (desc.minFilter == ImageFilter::Linear && desc.magFilter == ImageFilter::Linear && desc.mipmap == Mipmap::Linear) {
        samplerDesc.Filter = D3D12_FILTER_ANISOTROPIC;
    } else {
        u32 filterUint = 0x0;

        if (desc.mipmap == Mipmap::Linear) {
            filterUint |= 0x1;
        }

        if (desc.magFilter == ImageFilter::Linear) {
            filterUint |= 0x4;
        }

        if (desc.minFilter == ImageFilter::Linear) {
            filterUint |= 0x10;
        }

        ARKOSE_ASSERTM(filterUint == D3D12_FILTER_MIN_MAG_MIP_POINT
                    || filterUint == D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR
                    || filterUint == D3D12_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT
                    || filterUint == D3D12_FILTER_MIN_POINT_MAG_MIP_LINEAR
                    || filterUint == D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT
                    || filterUint == D3D12_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR
                    || filterUint == D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT
                    || filterUint == D3D12_FILTER_MIN_MAG_MIP_LINEAR,
                       "This combination of bits do not make up a valid filter");
        samplerDesc.Filter = static_cast<D3D12_FILTER>(filterUint);
    }

    auto wrapModeToAddressMode = [](ImageWrapMode mode) -> D3D12_TEXTURE_ADDRESS_MODE {
        switch (mode) {
        case ImageWrapMode::Repeat:
            return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        case ImageWrapMode::MirroredRepeat:
            return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
        case ImageWrapMode::ClampToEdge:
            return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        default:
            ASSERT_NOT_REACHED();
        }
    };

    samplerDesc.AddressU = wrapModeToAddressMode(desc.wrapMode.u);
    samplerDesc.AddressV = wrapModeToAddressMode(desc.wrapMode.v);
    samplerDesc.AddressW = wrapModeToAddressMode(desc.wrapMode.w);

    samplerDesc.MipLODBias = 0.0f;

    samplerDesc.MaxAnisotropy = 16;

    samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NONE;

    samplerDesc.BorderColor[0] = 0.0f;
    samplerDesc.BorderColor[1] = 0.0f;
    samplerDesc.BorderColor[2] = 0.0f;
    samplerDesc.BorderColor[3] = 0.0f;

    samplerDesc.MinLOD = 0.0f;
    samplerDesc.MaxLOD = 9999.0f;

    samplerDescriptor = d3d12Backend.samplerDescriptorHeapAllocator().allocate(1);
    d3d12Backend.device().CreateSampler(&samplerDesc, samplerDescriptor.firstCpuDescriptor);
}

D3D12Sampler::~D3D12Sampler()
{
}
