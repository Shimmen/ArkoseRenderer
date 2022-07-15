#include "D3D12Texture.h"

#include "utility/Profiling.h"
#include "core/Logging.h"

D3D12Texture::D3D12Texture(Backend& backend, Description desc)
    : Texture(backend, desc)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();
    
    // TODO
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

void D3D12Texture::setData(const void* data, size_t size)
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
