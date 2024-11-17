#include "Texture.h"

#include "asset/ImageAsset.h"
#include "core/Assert.h"
#include <ark/defer.h>
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
    case Texture::Format::R8Uint:
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
    }
}

Texture::MinFilter Texture::convertImageFilterToMinFilter(ImageFilter minFilter)
{
    switch (minFilter) {
    case ImageFilter::Nearest:
        return Texture::MinFilter::Nearest;
    case ImageFilter::Linear:
        return Texture::MinFilter::Linear;
    default:
        ASSERT_NOT_REACHED();
    }
}

Texture::MagFilter Texture::convertImageFilterToMagFilter(ImageFilter magFilter)
{
    switch (magFilter) {
    case ImageFilter::Nearest:
        return Texture::MagFilter::Nearest;
    case ImageFilter::Linear:
        return Texture::MagFilter::Linear;
    default:
        ASSERT_NOT_REACHED();
    }
}

Texture::Mipmap Texture::convertImageFilterToMipFilter(ImageFilter mipFilter, bool useMipmap)
{
    if (useMipmap) {
        switch (mipFilter) {
        case ImageFilter::Nearest:
            return Texture::Mipmap::Nearest;
        case ImageFilter::Linear:
            return Texture::Mipmap::Linear;
        default:
            ASSERT_NOT_REACHED();
        }
    } else {
        return Texture::Mipmap::None;
    }
}

Texture::Format Texture::convertImageFormatToTextureFormat(ImageFormat imageFormat, ImageType imageType)
{
    if (imageType == ImageType::sRGBColor) {
        switch (imageFormat) {
        case ImageFormat::RGBA8:
            return Format::sRGBA8;
        case ImageFormat::RGBA32F:
            return Format::RGBA32F;
        case ImageFormat::BC7:
            return Format::BC7sRGB;
        default:
            // TODO: Add fmt support for enums!
            ARKOSE_LOG(Warning, "Texture: using sRGB color space but no suitable image format ({}), exiting.", static_cast<int>(imageFormat));
        }
    }

    if (imageType == ImageType::NormalMap && imageFormat == ImageFormat::BC5) {
        return Format::BC5;
    }

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
    case ImageFormat::BC5:
        return Format::BC5;
    case ImageFormat::BC7:
        return Format::BC7;
    default:
        ARKOSE_LOG(Fatal, "No good conversion from image format {}", static_cast<int>(imageFormat));
    }
}

ImageFormat Texture::convertTextureFormatToImageFormat(Texture::Format textureFormat)
{
    switch (textureFormat) {
    case Texture::Format::R8:
        return ImageFormat::R8;
    case Texture::Format::R16F:
        NOT_YET_IMPLEMENTED();
    case Texture::Format::R32F:
        return ImageFormat::R32F;
    case Texture::Format::RG16F:
        NOT_YET_IMPLEMENTED();
    case Texture::Format::RG32F:
        return ImageFormat::RG32F;
    case Texture::Format::RGBA8:
        return ImageFormat::RGBA8;
    case Texture::Format::sRGBA8:
        return ImageFormat::RGBA8;
    case Texture::Format::RGBA16F:
        NOT_YET_IMPLEMENTED();
    case Texture::Format::RGBA32F:
        return ImageFormat::RGBA32F;
    case Texture::Format::Depth32F:
        return ImageFormat::R32F;
    case Texture::Format::Depth24Stencil8:
        // Not sure we can/should support this anyway..
        NOT_YET_IMPLEMENTED();
    case Texture::Format::R32Uint:
        NOT_YET_IMPLEMENTED();
    case Texture::Format::R8Uint:
        return ImageFormat::R8;
    case Texture::Format::BC5:
        return ImageFormat::BC5;
    case Texture::Format::BC7:
        return ImageFormat::BC7;
    case Texture::Format::BC7sRGB:
        return ImageFormat::BC7;
    case Texture::Format::Unknown:
        return ImageFormat::Unknown;
    default:
        ASSERT_NOT_REACHED();
    }
}

const Extent2D Texture::extentAtMip(uint32_t mip) const
{
    Extent3D mipExtent3D = extent3DAtMip(mip);
    return { mipExtent3D.width(), mipExtent3D.height() };
}

const Extent3D Texture::extent3DAtMip(uint32_t mip) const
{
    ARKOSE_ASSERT(mip < mipLevels());

    if (mip == 0) {
        return extent3D();
    }

    float p = std::pow(2.0f, static_cast<float>(mip));
    u32 x = static_cast<u32>(std::floor(extent3D().width() / p));
    u32 y = static_cast<u32>(std::floor(extent3D().height() / p));
    u32 z = static_cast<u32>(std::floor(extent3D().depth() / p));

    x = x > 1 ? x : 1;
    y = y > 1 ? y : 1;
    z = z > 1 ? z : 1;

    return { x, y, z };
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

void Texture::setPixelData(vec4 pixel)
{
    int numChannels = 0;
    bool isHdr = false;

    switch (format()) {
    case Texture::Format::R8:
        numChannels = 1;
        isHdr = false;
        break;
    case Texture::Format::R16F:
    case Texture::Format::R32F:
        numChannels = 1;
        isHdr = true;
        break;
    case Texture::Format::RG16F:
    case Texture::Format::RG32F:
        numChannels = 2;
        isHdr = true;
        break;
    case Texture::Format::RGBA8:
    case Texture::Format::sRGBA8:
        numChannels = 4;
        isHdr = false;
        break;
    case Texture::Format::RGBA16F:
    case Texture::Format::RGBA32F:
        numChannels = 4;
        isHdr = true;
        break;
    case Texture::Format::Depth32F:
        numChannels = 1;
        isHdr = true;
        break;
    case Texture::Format::R8Uint:
    case Texture::Format::R32Uint:
        numChannels = 1;
        isHdr = false;
        break;
    case Texture::Format::Unknown:
        ASSERT_NOT_REACHED();
        break;
    default:
        ARKOSE_LOG(Fatal, "Texture::setPixelData: unhandled texture format");
    }

    ARKOSE_ASSERT(numChannels == 4);

    if (isHdr) {
        setData(&pixel, sizeof(pixel), 0, 0);
    } else {
        ark::u8 pixelUint8Data[4];
        pixelUint8Data[0] = (ark::u8)(ark::clamp(pixel.x, 0.0f, 1.0f) * 255.99f);
        pixelUint8Data[1] = (ark::u8)(ark::clamp(pixel.y, 0.0f, 1.0f) * 255.99f);
        pixelUint8Data[2] = (ark::u8)(ark::clamp(pixel.z, 0.0f, 1.0f) * 255.99f);
        pixelUint8Data[3] = (ark::u8)(ark::clamp(pixel.w, 0.0f, 1.0f) * 255.99f);
        setData(pixelUint8Data, sizeof(pixelUint8Data), 0, 0);
    }
}

std::unique_ptr<Texture> Texture::createFromPixel(Backend& backend, vec4 pixelColor, bool sRGB)
{
    SCOPED_PROFILE_ZONE();

    Texture::Description desc {
        .type = Texture::Type::Texture2D,
        .arrayCount = 1u,
        .extent = Extent3D(1, 1, 1),
        .format = sRGB
            ? Texture::Format::sRGBA8
            : Texture::Format::RGBA8,
        .filter = Texture::Filters::nearest(),
        .wrapMode = ImageWrapModes::repeatAll(),
        .mipmap = Texture::Mipmap::None,
        .multisampling = Texture::Multisampling::None
    };

    auto texture = backend.createTexture(desc);
    texture->setPixelData(pixelColor);

    return texture;
}

std::unique_ptr<Texture> Texture::createFromImagePathSequence(Backend& backend, const std::string& imagePathSequencePattern, bool sRGB, bool generateMipmaps, ImageWrapModes)
{
    SCOPED_PROFILE_ZONE();

    // TODO: Make this be not incredibly slow.. e.g. don't load all of them individually like this
    //       We now support multithreaded loading, but the "right" solution is to store them all in
    //       a single file, e.g. a compressed binary blob or some proper format with layer support,
    //       such as OpenEXR.

    //size_t totalRequiredSize = 0;
    std::vector<ImageAsset*> imageAssets;
    for (size_t idx = 0;; ++idx) {
        std::string imagePath = fmt::vformat(imagePathSequencePattern, fmt::make_format_args(idx));
        ImageAsset* imageAsset = ImageAsset::loadOrCreate(imagePath);
        if (!imageAsset)
            break;
        // TODO: Support multiple mips!
        //totalRequiredSize += imageAsset->pixelDataForMip(0).size();
        imageAssets.push_back(imageAsset);
    }

    if (imageAssets.size() == 0) {
        ARKOSE_LOG(Fatal, "Registry: could not find any images in image array pattern <{}>, exiting", imagePathSequencePattern);
    }

    // Use the first one as "prototype" image asset
    ImageAsset const& asset0 = *imageAssets.front();
    uint32_t arrayCount = static_cast<uint32_t>(imageAssets.size());

    // Ensure all are similar (doesn't cover all cases, but it's something)
    for (uint32_t idx = 1; idx < arrayCount; ++idx) {
        ImageAsset const& otherAsset = *imageAssets[idx];
        ARKOSE_ASSERT(asset0.width() == otherAsset.width() && asset0.height() == otherAsset.height());
    }

    ImageType colorSpace = sRGB ? ImageType::sRGBColor : ImageType::GenericData;
    Texture::Format format = Texture::convertImageFormatToTextureFormat(asset0.format(), colorSpace);

    auto mipmapMode = (generateMipmaps && asset0.width() > 1 && asset0.height() > 1)
        ? Texture::Mipmap::Linear
        : Texture::Mipmap::None;

    // TODO: Handle other than Texture2D arrays
    Texture::Description desc {
        .type = Texture::Type::Texture2D,
        .arrayCount = arrayCount,
        .extent = { asset0.width(), asset0.height(), 1 },
        .format = format,
        .filter = Texture::Filters::linear(),
        .wrapMode = ImageWrapModes::repeatAll(),
        .mipmap = mipmapMode,
        .multisampling = Texture::Multisampling::None
    };

    auto texture = backend.createTexture(desc);
    texture->setName("Texture:" + imagePathSequencePattern);

    // Allocate temporary storage for pixel data ahead of upload to texture
    // TODO: Maybe we can just map the individual image into memory directly?
    //uint8_t* textureArrayMemory = static_cast<uint8_t*>(malloc(totalRequiredSize));
    //ark::AtScopeExit freeMemory { [&]() { free(textureArrayMemory); } };

    // Load images and set texture data
    for (size_t imageIdx = 0; imageIdx < imageAssets.size(); ++imageIdx) {

        ImageAsset const& imageAsset = *imageAssets[imageIdx];
        auto const& data = imageAsset.pixelDataForMip(0);

        //size_t offset = imageIdx * data.size();
        //std::memcpy(textureArrayMemory + offset, data.data(), data.size());

        // TODO: This is not very optimal at all.. a lot of staging buffers being created and teared down contantly..
        texture->setData(data.data(), data.size(), 0, imageIdx);
    }

    return texture;
}
