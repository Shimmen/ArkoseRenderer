#include "Texture.h"

#include "asset/ImageAsset.h"
#include "core/Assert.h"
#include "core/Defer.h"
#include "core/Logging.h"
#include "core/parallel/ParallelFor.h"
#include "rendering/backend/base/Backend.h"
#include <cmath>
#include <fmt/format.h>

Texture::Texture(Backend& backend, Description desc)
    : Resource(backend)
    , m_description(desc)
{
    // (according to most specifications we can't have both multisampling and mipmapping)
#pragma warning(push)
#pragma warning(disable : 26813)
    ARKOSE_ASSERT(multisampling() == Multisampling::None || mipmap() == Mipmap::None);
#pragma warning(pop)

    // At least one item in an implicit array
    ARKOSE_ASSERT(arrayCount() > 0);
}

bool Texture::hasFloatingPointDataFormat() const
{
    switch (format()) {
    case Texture::Format::R8:
    case Texture::Format::R32Uint:
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

Texture::Format Texture::convertImageFormatToTextureFormat(ImageFormat imageFormat, ColorSpace colorSpace)
{
    if (colorSpace == ColorSpace::sRGB_encoded) {
        switch (imageFormat) {
        case ImageFormat::RGBA8:
            return Format::sRGBA8;
        case ImageFormat::RGBA32F:
            return Format::RGBA32F;
        case ImageFormat::BC7:
            NOT_YET_IMPLEMENTED();
            //return Format::BC7sRGB;
        }

        // TODO: Add fmt support for flatbuffers enums!
        ARKOSE_LOG_FATAL("Texture: using sRGB color space but no suitabe image format ({}), exiting.", static_cast<int>(imageFormat));
    }

    // From now on, no sRGB ...

    switch (imageFormat) {
    case ImageFormat::R8:
        return Format::R8;
    case ImageFormat::RG8:
        NOT_YET_IMPLEMENTED();
    case ImageFormat::RGB8:
        NOT_YET_IMPLEMENTED();
    case ImageFormat::RGBA8:
        return Format::RGBA8;
    case ImageFormat::R32F:
        return Format::R32F;
    case ImageFormat::RG32F:
        return Format::RG32F;
    case ImageFormat::RGB32F:
        NOT_YET_IMPLEMENTED();
    case ImageFormat::RGBA32F:
        return Format::RGBA32F;
    case ImageFormat::BC7:
        NOT_YET_IMPLEMENTED();
    }

    // TODO: Add fmt support for flatbuffers enums!
    ARKOSE_LOG_FATAL("No good conversion from image format {}", static_cast<int>(imageFormat));
    return Format::Unknown;
}

const Extent2D Texture::extentAtMip(uint32_t mip) const
{
    ARKOSE_ASSERT(mip < mipLevels());

    if (mip == 0) {
        return extent();
    }

    // TODO: We can make this non-looping..
    uint32_t x = extent().width();
    uint32_t y = extent().height();
    for (uint32_t i = 0; i < mip; ++i) {
        x /= 2;
        y /= 2;
    }

    x = x > 1 ? x : 1;
    y = y > 1 ? y : 1;

    return { x, y };
}

bool Texture::hasMipmaps() const
{
    return mipmap() != Mipmap::None;
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
    return multisampling() != Multisampling::None;
}

Texture::Multisampling Texture::multisampling() const
{
    return m_description.multisampling;
}

void Texture::pixelFormatAndTypeForImageInfo(const Image::Info& info, bool sRGB, Texture::Format& format, Image::PixelType& pixelTypeToUse)
{
    switch (info.pixelType) {
    case Image::PixelType::RGB:
    case Image::PixelType::RGBA:
        // Honestly, this is easier to read than the if-based equivalent..
        format = (info.isHdr())
            ? Texture::Format::RGBA32F
            : (sRGB)
                ? Texture::Format::sRGBA8
                : Texture::Format::RGBA8;
        // RGB formats aren't always supported, so always use RGBA for 3-component data
        pixelTypeToUse = Image::PixelType::RGBA;
        break;
    default:
        ARKOSE_LOG(Fatal, "Texture: currently no support for other than (s)RGB(F) and (s)RGBA(F) texture loading!");
    }
}

std::unique_ptr<Texture> Texture::createFromImage(Backend& backend, const Image& image, bool sRGB, bool generateMipmaps, Texture::WrapModes wrapMode)
{
    SCOPED_PROFILE_ZONE()

    auto mipmapMode = (generateMipmaps && image.info().width > 1 && image.info().height > 1)
        ? Texture::Mipmap::Linear
        : Texture::Mipmap::None;

    Texture::Format format {};
    int numDesiredComponents {};
    int pixelSizeBytes {};

    switch (image.info().pixelType) {
    case Image::PixelType::Grayscale:
        numDesiredComponents = 1;
        if (!sRGB && image.info().isHdr()) {
            format = Texture::Format::R32F;
            pixelSizeBytes = sizeof(float);
        } else {
            ARKOSE_LOG(Fatal, "Registry: no support for grayscale non-HDR or sRGB texture loading (from image)!");
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
        ARKOSE_LOG(Fatal, "Registry: currently no support for other than R32F, (s)RGB(F), and (s)RGBA(F) texture loading (from image)!");
    }

    Texture::Description desc {
        .type = Texture::Type::Texture2D,
        .arrayCount = 1u,
        .extent = { (uint32_t)image.info().width, (uint32_t)image.info().height, 1 },
        .format = format,
        .filter = Texture::Filters::linear(),
        .wrapMode = wrapMode,
        .mipmap = mipmapMode,
        .multisampling = Texture::Multisampling::None
    };

    //validateTextureDescription(desc);
    auto texture = backend.createTexture(desc);

    // TODO: Also handle compressed data! requiredStorageSize() should do most heavy lifting,
    // but we do have to create the correct image format up above too.
    ARKOSE_ASSERT(image.info().compressionType == Image::CompressionType::Uncompressed);
    texture->setData(image.data(), image.dataSize());

    return texture;
}

std::unique_ptr<Texture> Texture::createFromPixel(Backend& backend, vec4 pixelColor, bool sRGB)
{
    SCOPED_PROFILE_ZONE()

    Texture::Description desc {
        .type = Texture::Type::Texture2D,
        .arrayCount = 1u,
        .extent = Extent3D(1, 1, 1),
        .format = sRGB
            ? Texture::Format::sRGBA8
            : Texture::Format::RGBA8,
        .filter = Texture::Filters::nearest(),
        .wrapMode = Texture::WrapModes::repeatAll(),
        .mipmap = Texture::Mipmap::None,
        .multisampling = Texture::Multisampling::None
    };

    auto texture = backend.createTexture(desc);
    texture->setPixelData(pixelColor);

    return texture;
}


std::unique_ptr<Texture> Texture::createFromImagePath(Backend& backend, const std::string& imagePath, bool sRGB, bool generateMipmaps, Texture::WrapModes wrapModes)
{
    SCOPED_PROFILE_ZONE();

    // FIXME (maybe): Add async loading?

    if (ImageAsset* imageAsset = ImageAsset::loadOrCreate(imagePath)) {

        auto mipmapMode = (generateMipmaps && imageAsset->width > 1 && imageAsset->height > 1)
            ? Texture::Mipmap::Linear
            : Texture::Mipmap::None;

        Texture::Description desc {

            // TODO: Support other types than non-array texture 2D?
            .type = Texture::Type::Texture2D,
            .arrayCount = 1u,

            .extent = { imageAsset->width, imageAsset->height, imageAsset->depth },
            .format = convertImageFormatToTextureFormat(imageAsset->format, imageAsset->color_space),
            .filter = Texture::Filters::linear(),
            .wrapMode = wrapModes,
            .mipmap = mipmapMode,

            .multisampling = Texture::Multisampling::None
        };

        auto texture = backend.createTexture(desc);
        texture->setName("Texture:" + imagePath);

        texture->setData(imageAsset->pixel_data.data(), imageAsset->pixel_data.size());

        return texture;

    }

    return nullptr;
}

std::unique_ptr<Texture> Texture::createFromImagePathSequence(Backend& backend, const std::string& imagePathSequencePattern, bool sRGB, bool generateMipmaps, Texture::WrapModes)
{
    SCOPED_PROFILE_ZONE()

    // TODO: Make this be not incredibly slow.. e.g. don't load all of them individually like this
    //       We now support multithreaded loading, but the "right" solution is to store them all in
    //       a single file, e.g. a compressed binary blob or some proper format with layer support,
    //       such as OpenEXR.

    size_t totalRequiredSize = 0;
    std::vector<std::string> imagePaths;
    std::vector<Image::Info*> imageInfos;
    for (size_t idx = 0;; ++idx) {
        std::string imagePath = fmt::format(fmt::runtime(imagePathSequencePattern), idx);
        Image::Info* imageInfo = Image::getInfo(imagePath, true);
        if (!imageInfo)
            break;
        totalRequiredSize += imageInfo->requiredStorageSize();
        imageInfos.push_back(std::move(imageInfo));
        imagePaths.push_back(std::move(imagePath));
    }

    if (imageInfos.size() == 0)
        ARKOSE_LOG(Fatal, "Registry: could not find any images in image array pattern <{}>, exiting", imagePathSequencePattern);

    // Use the first one as "prototype" image info
    Image::Info& info = *imageInfos.front();
    uint32_t arrayCount = static_cast<uint32_t>(imageInfos.size());

    // Ensure all are similar
    for (uint32_t idx = 1; idx < arrayCount; ++idx) {
        Image::Info& otherInfo = *imageInfos[idx];
        ARKOSE_ASSERT(info == otherInfo);
    }

    Texture::Format format;
    Image::PixelType pixelTypeToUse;
    Texture::pixelFormatAndTypeForImageInfo(info, sRGB, format, pixelTypeToUse);

    auto mipmapMode = (generateMipmaps && info.width > 1 && info.height > 1)
        ? Texture::Mipmap::Linear
        : Texture::Mipmap::None;

    Texture::Description desc {
        .type = Texture::Type::Texture2D,
        .arrayCount = arrayCount,
        .extent = { (uint32_t)info.width, (uint32_t)info.height, 1 },
        .format = format,
        .filter = Texture::Filters::linear(),
        .wrapMode = Texture::WrapModes::repeatAll(),
        .mipmap = mipmapMode,
        .multisampling = Texture::Multisampling::None
    };

    auto texture = backend.createTexture(desc);
    texture->setName("Texture:" + imagePathSequencePattern);

    // Allocate temporary storage for pixel data ahead of upload to texture
    // TODO: Maybe we can just map the individual image into memory directly?
    uint8_t* textureArrayMemory = static_cast<uint8_t*>(malloc(totalRequiredSize));
    AtScopeExit freeMemory { [&]() { free(textureArrayMemory); } };

    // Load images and set texture data
    // TODO: Ensure this is not completely starved by async material texture loading!
    constexpr bool UseSingleThreadedLoading = true;
    ParallelFor(imagePaths.size(), [&](size_t idx) {

        const std::string& imagePath = imagePaths[idx];
        Image* image = Image::load(imagePath, pixelTypeToUse, true);

        size_t offset = idx * image->dataSize();
        std::memcpy(textureArrayMemory + offset, image->data(), image->dataSize());

    }, UseSingleThreadedLoading);
    texture->setData(textureArrayMemory, totalRequiredSize);

    return texture;
}
