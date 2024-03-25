#include "D3D12Texture.h"

#include "core/Logging.h"
#include "rendering/backend/d3d12/D3D12Backend.h"
#include "rendering/backend/d3d12/D3D12Buffer.h"
#include "utility/Profiling.h"

D3D12Texture::D3D12Texture(Backend& backend, Description desc)
    : Texture(backend, desc)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    auto& d3d12Backend = static_cast<D3D12Backend&>(backend);

    bool storageCapable = true;
    bool attachmentCapable = true;
    bool depthStencilCapable = false;

    switch (format()) {
    case Texture::Format::R8:
        dxgiFormat = DXGI_FORMAT_R8_UNORM;
        break;
    case Texture::Format::RGBA8:
        dxgiFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        break;
    case Texture::Format::sRGBA8:
        dxgiFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        storageCapable = false;
        break;
    case Texture::Format::R16F:
        dxgiFormat = DXGI_FORMAT_R16_FLOAT;
        break;
    case Texture::Format::R32F:
        dxgiFormat = DXGI_FORMAT_R32_FLOAT;
        break;
    case Texture::Format::RG16F:
        dxgiFormat = DXGI_FORMAT_R16G16_FLOAT;
        break;
    case Texture::Format::RG32F:
        dxgiFormat = DXGI_FORMAT_R32G32_FLOAT;
        break;
    case Texture::Format::RGBA16F:
        dxgiFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
        break;
    case Texture::Format::RGBA32F:
        dxgiFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
        break;
    case Texture::Format::Depth32F:
        dxgiFormat = DXGI_FORMAT_D32_FLOAT;
        storageCapable = false;
        attachmentCapable = false;
        depthStencilCapable = true;
        break;
    case Texture::Format::Depth24Stencil8:
        dxgiFormat = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        storageCapable = false;
        attachmentCapable = false;
        depthStencilCapable = true;
        break;
    case Texture::Format::R32Uint:
        dxgiFormat = DXGI_FORMAT_R32_UINT;
        break;
    case Texture::Format::BC5:
        dxgiFormat = DXGI_FORMAT_BC5_UNORM;
        storageCapable = false;
        attachmentCapable = false;
        break;
    case Texture::Format::BC7:
        dxgiFormat = DXGI_FORMAT_BC7_UNORM;
        storageCapable = false;
        attachmentCapable = false;
        break;
    case Texture::Format::BC7sRGB:
        dxgiFormat = DXGI_FORMAT_BC7_UNORM_SRGB;
        storageCapable = false;
        attachmentCapable = false;
        break;
    case Texture::Format::Unknown:
        ARKOSE_LOG_FATAL("D3D12Texture: Trying to create new texture with format Unknown, which is not allowed!");
    default:
        ASSERT_NOT_REACHED();
    }

    // Not sure if this is possible in D3D12? Might as well assume no for now.
    if (multisampling() != Texture::Multisampling::None) {
        storageCapable = false;
    }

    textureDescription = {};
    textureDescription.Alignment = 0;

    textureDescription.MipLevels = narrow_cast<u16>(mipLevels());
    textureDescription.Format = dxgiFormat;

    switch (type()) {
    case Type::Texture2D:
        textureDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        textureDescription.Width = extent().width();
        textureDescription.Height = extent().height();
        textureDescription.DepthOrArraySize = narrow_cast<u16>(arrayCount());
        break;
    case Type::Cubemap:
        NOT_YET_IMPLEMENTED();
        break;
    default:
        ASSERT_NOT_REACHED();
    }

    textureDescription.SampleDesc.Count = static_cast<UINT>(multisampling());
    textureDescription.SampleDesc.Quality = isMultisampled() ? DXGI_STANDARD_MULTISAMPLE_QUALITY_PATTERN : 0;

    textureDescription.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    textureDescription.Flags = D3D12_RESOURCE_FLAG_NONE;
    if (attachmentCapable) {
        textureDescription.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    }
    if (depthStencilCapable) {
        textureDescription.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    }
    if (storageCapable) {
        textureDescription.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }

    D3D12_CLEAR_VALUE optimizedClearValue;
    optimizedClearValue.Format = dxgiFormat;
    if (hasDepthFormat()) {
        optimizedClearValue.DepthStencil.Depth = 1.0f;
        optimizedClearValue.DepthStencil.Stencil = 0u;
    } else {
        optimizedClearValue.Color[0] = 0.0f;
        optimizedClearValue.Color[1] = 0.0f;
        optimizedClearValue.Color[2] = 0.0f;
        optimizedClearValue.Color[3] = 0.0f;
    }

    CD3DX12_HEAP_PROPERTIES heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_RESOURCE_STATES initialResourceState = resourceState;

    // TODO: Don't use commited resource! Sub-allocate instead
    auto hr = d3d12Backend.device().CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE,
                                                            &textureDescription, initialResourceState,
                                                            &optimizedClearValue,
                                                            IID_PPV_ARGS(&textureResource));
    if (FAILED(hr)) {
        ARKOSE_LOG(Fatal, "D3D12Texture: could not create committed resource for texture, exiting.");
    }

    if (!hasDepthFormat()) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc {};
        srvDesc.Format = textureDescription.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = mipLevels();
        srvDesc.Texture2D.PlaneSlice = 0;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

        srvDescriptor = d3d12Backend.copyableDescriptorHeapAllocator().allocate(1);
        d3d12Backend.device().CreateShaderResourceView(textureResource.Get(), &srvDesc, srvDescriptor.firstCpuDescriptor);
    }

    if (storageCapable) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc {};
        uavDesc.Format = textureDescription.Format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = 0;
        uavDesc.Texture2D.PlaneSlice = 0;

        uavDescriptor = d3d12Backend.copyableDescriptorHeapAllocator().allocate(1);
        d3d12Backend.device().CreateUnorderedAccessView(textureResource.Get(), nullptr, &uavDesc, uavDescriptor.firstCpuDescriptor);
    }
}

D3D12Texture::~D3D12Texture()
{
    if (!hasBackend())
        return;
    // TODO
}

void D3D12Texture::setName(const std::string& name)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    Resource::setName(name);
    textureResource->SetName(convertToWideString(name).c_str());
}

void D3D12Texture::clear(ClearColor color)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    // TODO
}

void D3D12Texture::setData(const void* data, size_t size, size_t mipIdx, size_t arrayIdx)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    auto& d3d12Backend = static_cast<D3D12Backend&>(backend());

    // Other types are not yet implemented!
    ARKOSE_ASSERT(type() == Texture::Type::Texture2D);
    u32 subresourceIdx = narrow_cast<u32>(mipIdx + arrayIdx * mipLevels());

    u64 textureMemorySize = 0;
    u32 numRows;
    u64 rowSizeInBytes;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT subresourceFootprint;

    d3d12Backend.device().GetCopyableFootprints(&textureDescription,
                                                subresourceIdx, 1, 0,
                                                &subresourceFootprint,
                                                &numRows, &rowSizeInBytes,
                                                &textureMemorySize);

    auto stagingBuffer = std::make_unique<D3D12Buffer>(d3d12Backend, textureMemorySize, Buffer::Usage::Transfer, Buffer::MemoryHint::TransferOptimal);
    d3d12Backend.setBufferDataUsingMapping(*stagingBuffer->bufferResource.Get(), static_cast<u8 const*>(data), size);

    d3d12Backend.issueUploadCommand([&](ID3D12GraphicsCommandList& cmdList) {
        D3D12_RESOURCE_BARRIER resourceBarrier {};
        resourceBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        resourceBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        resourceBarrier.Transition.pResource = textureResource.Get();
        resourceBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        if (resourceState != D3D12_RESOURCE_STATE_COPY_DEST) {
            resourceBarrier.Transition.StateBefore = resourceState;
            resourceBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            cmdList.ResourceBarrier(1, &resourceBarrier);
        }

        D3D12_TEXTURE_COPY_LOCATION source {};
        source.pResource = stagingBuffer->bufferResource.Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint = subresourceFootprint;

        D3D12_TEXTURE_COPY_LOCATION destination {};
        destination.pResource = textureResource.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.SubresourceIndex = subresourceIdx;

        cmdList.CopyTextureRegion(&destination, 0, 0, 0,
                                  &source, nullptr);

        if (resourceState != D3D12_RESOURCE_STATE_COPY_DEST) {
            resourceBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            resourceBarrier.Transition.StateAfter = resourceState;
            cmdList.ResourceBarrier(1, &resourceBarrier);
        }
    });
}

void D3D12Texture::generateMipmaps()
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    if (!hasMipmaps()) {
        ARKOSE_LOG(Error, "D3D12Texture: generateMipmaps() called on texture which doesn't have space for mipmaps allocated. Ignoring request.");
        return;
    }
    
    // TODO
}

ImTextureID D3D12Texture::asImTextureID()
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    if (!srvNoAlphaDesciptorForImGui.valid()) {
        auto& d3d12Backend = static_cast<D3D12Backend&>(backend());

        // No need to ever move this descriptor so might as well put it directly into the shader visible heap
        srvNoAlphaDesciptorForImGui = d3d12Backend.shaderVisibleDescriptorHeapAllocator().allocate(1);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc {};
        srvDesc.Format = textureDescription.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;

        // NOTE: Don't render with alpha for the ImGui textures
        srvDesc.Shader4ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0,
                                                                                  D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_1,
                                                                                  D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_2,
                                                                                  D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1);
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = mipLevels();
        srvDesc.Texture2D.PlaneSlice = 0;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

        d3d12Backend.device().CreateShaderResourceView(textureResource.Get(), &srvDesc, srvNoAlphaDesciptorForImGui.firstCpuDescriptor);
    }

    return reinterpret_cast<ImTextureID>(srvNoAlphaDesciptorForImGui.firstGpuDescriptor.ptr);
}
