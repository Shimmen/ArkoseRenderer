#include "Texture.h"

#include "backend/base/Backend.h"
#include "utility/Image.h"
#include "utility/Logging.h"
#include "utility/util.h"
#include <cmath>
#include <stb_image.h>

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

std::unique_ptr<Texture> Texture::createFromImage(Backend& backend, const Image& image, bool sRGB, bool generateMipmaps, Texture::WrapModes wrapMode)
{
    auto mipmapMode = (generateMipmaps && image.info().width > 1 && image.info().height > 1)
        ? Texture::Mipmap::Linear
        : Texture::Mipmap::None;

    Texture::Format format;
    int numDesiredComponents;
    int pixelSizeBytes;

    switch (image.info().pixelType) {
    case Image::PixelType::Grayscale:
        numDesiredComponents = 1;
        if (!sRGB && image.info().isHdr()) {
            format = Texture::Format::R32F;
            pixelSizeBytes = sizeof(float);
        } else {
            LogErrorAndExit("Registry: no support for grayscale non-HDR or sRGB texture loading (from image)!\n");
        }
        break;
    case Image::PixelType::RGB:
    case Image::PixelType::RGBA:
        numDesiredComponents = 4;
        if (image.info().isHdr()) {
            format = Texture::Format::RGBA32F;
            pixelSizeBytes = 4 * sizeof(float);
        } else {
            format = (sRGB)
                ? Texture::Format::sRGBA8
                : Texture::Format::RGBA8;
            pixelSizeBytes = 4 * sizeof(uint8_t);
        }
        break;
    default:
        LogErrorAndExit("Registry: currently no support for other than R32F, (s)RGB(F), and (s)RGBA(F) texture loading (from image)!\n");
    }

    Texture::TextureDescription desc {
        .type = Texture::Type::Texture2D,
        .arrayCount = 1u,
        .extent = { (uint32_t)image.info().width, (uint32_t)image.info().height, 1 },
        .format = format,
        .minFilter = Texture::MinFilter::Linear,
        .magFilter = Texture::MagFilter::Linear,
        .wrapMode = wrapMode,
        .mipmap = mipmapMode,
        .multisampling = Texture::Multisampling::None
    };

    //validateTextureDescription(desc);
    auto texture = Backend::get().createTexture(desc);

    int width, height;
    const void* rawPixelData;
    switch (image.dataOwner()) {
    case Image::DataOwner::StbImage:
        if (image.info().isHdr())
            rawPixelData = (void*)stbi_loadf_from_memory((const stbi_uc*)image.data(), (int)image.size(), &width, &height, nullptr, numDesiredComponents);
        else
            rawPixelData = (void*)stbi_load_from_memory((const stbi_uc*)image.data(), (int)image.size(), &width, &height, nullptr, numDesiredComponents);
        ASSERT(width == image.info().width);
        ASSERT(height == image.info().height);
        break;
    case Image::DataOwner::External:
        rawPixelData = image.data();
        width = image.info().width;
        height = image.info().height;
        break;
    default:
        ASSERT_NOT_REACHED();
        break;
    }

    uint32_t rawDataSize = width * height * pixelSizeBytes;
    texture->setData(rawPixelData, rawDataSize);

    if (image.dataOwner() == Image::DataOwner::StbImage)
        stbi_image_free(const_cast<void*>(rawPixelData));

    return texture;
}
