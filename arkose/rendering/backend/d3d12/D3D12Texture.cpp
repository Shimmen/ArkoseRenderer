#include "D3D12Texture.h"

#include "core/Logging.h"
#include "rendering/backend/d3d12/D3D12Backend.h"
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

    D3D12_RESOURCE_DESC bufferDescription = {};
    bufferDescription.Alignment = 0;

    bufferDescription.MipLevels = narrow_cast<u16>(mipLevels());
    bufferDescription.Format = dxgiFormat;

    switch (type()) {
    case Type::Texture2D:
        bufferDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        bufferDescription.Width = extent().width();
        bufferDescription.Height = extent().height();
        bufferDescription.DepthOrArraySize = narrow_cast<u16>(arrayCount());
        break;
    case Type::Cubemap:
        NOT_YET_IMPLEMENTED();
        break;
    default:
        ASSERT_NOT_REACHED();
    }

    bufferDescription.SampleDesc.Count = static_cast<UINT>(multisampling());
    bufferDescription.SampleDesc.Quality = isMultisampled() ? DXGI_STANDARD_MULTISAMPLE_QUALITY_PATTERN : 0;

    bufferDescription.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    bufferDescription.Flags = D3D12_RESOURCE_FLAG_NONE;
    if (attachmentCapable) {
        bufferDescription.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    }
    if (depthStencilCapable) {
        bufferDescription.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    }
    if (storageCapable) {
        bufferDescription.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }

    CD3DX12_HEAP_PROPERTIES heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_RESOURCE_STATES initialResourceState = D3D12_RESOURCE_STATE_COMMON;

    // TODO: Don't use commited resource! Sub-allocate instead
    auto hr = d3d12Backend.device().CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE,
                                                            &bufferDescription, initialResourceState, nullptr,
                                                            IID_PPV_ARGS(&textureResource));
    if (FAILED(hr)) {
        ARKOSE_LOG(Fatal, "D3D12Texture: could not create committed resource for texture, exiting.");
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

    // TODO
}

void D3D12Texture::clear(ClearColor color)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    // TODO
}

void D3D12Texture::setPixelData(vec4 pixel)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    // TODO
}

void D3D12Texture::setData(const void* data, size_t size, size_t mipIdx)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    // TODO
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

    //TODO

    return ImTextureID();
}
