#include "Texture.h"

#include "utility/util.h"
#include <cmath>

Texture::Texture(Backend& backend, TextureDescription desc)
    : Resource(backend)
    , m_type(desc.type)
    , m_arrayCount(desc.arrayCount)
    , m_extent(desc.extent)
    , m_format(desc.format)
    , m_minFilter(desc.minFilter)
    , m_magFilter(desc.magFilter)
    , m_wrapMode(desc.wrapMode)
    , m_mipmap(desc.mipmap)
    , m_multisampling(desc.multisampling)
{
    // (according to most specifications we can't have both multisampling and mipmapping)
    ASSERT(m_multisampling == Multisampling::None || m_mipmap == Mipmap::None);

    // At least one item in an implicit array
    ASSERT(m_arrayCount > 0);
}

bool Texture::hasFloatingPointDataFormat() const
{
    switch (format()) {
    case Texture::Format::R32:
    case Texture::Format::RGBA8:
    case Texture::Format::sRGBA8:
        return false;
    case Texture::Format::R16F:
    case Texture::Format::RGBA16F:
    case Texture::Format::RGBA32F:
    case Texture::Format::Depth32F:
        return true;
    case Texture::Format::Unknown:
    default:
        ASSERT_NOT_REACHED();
        return false;
    }
}

bool Texture::hasMipmaps() const
{
    return m_mipmap != Mipmap::None;
}

uint32_t Texture::mipLevels() const
{
    if (hasMipmaps()) {
        uint32_t size = std::max(extent().width(), extent().height());
        uint32_t levels = static_cast<uint32_t>(std::floor(std::log2(size)) + 1);
        return levels;
    } else {
        return 1;
    }
}

bool Texture::isMultisampled() const
{
    return m_multisampling != Multisampling::None;
}

Texture::Multisampling Texture::multisampling() const
{
    return m_multisampling;
}
