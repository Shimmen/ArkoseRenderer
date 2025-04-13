#include "ImageAsset.h"

#include "asset/AssetCache.h"
#include "asset/external/DDSImage.h"
#include "core/Assert.h"
#include "core/Logging.h"
#include "utility/FileIO.h"
#include "utility/Profiling.h"
#include <cereal/cereal.hpp>
#include <cereal/archives/binary.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include <fmt/format.h>
#include <fstream>
#include <stb_image.h>

namespace {
AssetCache<ImageAsset> s_imageAssetCache {};
}

bool imageFormatIsBlockCompressed(ImageFormat format)
{
    switch (format) {
    case ImageFormat::BC5:
    case ImageFormat::BC7:
        return true;
    default:
        return false;
    }
}

u32 imageFormatBlockSize(ImageFormat format)
{
    switch (format) {
    case ImageFormat::BC5:
    case ImageFormat::BC7:
        return 16;
    default:
        if (imageFormatIsBlockCompressed(format)) { 
            NOT_YET_IMPLEMENTED();
        } else {
            ASSERT_NOT_REACHED();
        }
    }
}

ImageAsset::ImageAsset() = default;
ImageAsset::~ImageAsset() = default;

std::unique_ptr<ImageAsset> ImageAsset::createCopyWithReplacedFormat(ImageAsset const& inputImage, ImageFormat newFormat, std::vector<u8>&& pixelData, std::vector<ImageMip> imageMips)
{
    auto newImage = std::make_unique<ImageAsset>();

    newImage->name = inputImage.name;

    newImage->m_extent = inputImage.m_extent;
    newImage->m_type = inputImage.m_type;
    newImage->m_sourceAssetFilePath = inputImage.m_sourceAssetFilePath;

    newImage->m_format = newFormat;
    newImage->m_pixelData = std::move(pixelData);
    newImage->m_mips = std::move(imageMips);

    return newImage;
}

std::unique_ptr<ImageAsset> ImageAsset::createFromSourceAsset(std::filesystem::path const& sourceAssetFilePath)
{
    SCOPED_PROFILE_ZONE();

    auto maybeData = FileIO::readBinaryDataFromFile<uint8_t>(sourceAssetFilePath);
    if (not maybeData.has_value()) {
        return nullptr;
    }

    uint8_t* data = maybeData.value().data();
    size_t size = maybeData.value().size();

    auto imageAsset = createFromSourceAsset(data, size);
    imageAsset->m_sourceAssetFilePath = sourceAssetFilePath.string();

    return imageAsset;
}

std::unique_ptr<ImageAsset> ImageAsset::createFromSourceAsset(uint8_t const* sourceAssetData, size_t sourceAssetSize)
{
    SCOPED_PROFILE_ZONE();

    std::unique_ptr<ImageAsset> imageAsset = nullptr;

    if (DDS::isValidHeader(sourceAssetData, sourceAssetSize)) {

        // TODO: Get rid of this path! It's no longer a source asset format!

#if 0
        Extent3D extent;
        ImageFormat format;
        bool srgb;
        u32 numMips;
        void* data = (void*)DDS::loadFromMemory(sourceAssetData, sourceAssetSize, extent, format, srgb, numMips);

        if (data == nullptr) {
            ARKOSE_LOG(Error, "Failed to load image asset (DDS reported error, likely invalid file)");
            return nullptr;
        }

        imageAsset = std::make_unique<ImageAsset>();

        imageAsset->m_extent = extent;

        imageAsset->m_format = format;
        imageAsset->m_type = srgb ? ImageType::sRGBColor : ImageType::Unknown;

        imageAsset->m_mips = DDS::computeMipOffsetAndSize(extent, format, numMips);

        uint8_t* dataPtr = reinterpret_cast<uint8_t*>(data);
        ARKOSE_ASSERT(imageAsset->m_mips[0].offset == 0); // we assume all the mips are laid out sequentially
        size_t dataSize = imageAsset->m_mips.back().offset + imageAsset->m_mips.back().size;
        imageAsset->m_pixelData = std::vector<uint8_t>(dataPtr, dataPtr + dataSize);
#endif

    } else {

        bool isFloatType = false;
        int width, height, channelsInFile;

        int success = stbi_info_from_memory(sourceAssetData, static_cast<int>(sourceAssetSize), &width, &height, &channelsInFile);

        if (!success) {
            ARKOSE_LOG(Error, "Failed to load to load image asset (stb reported error, likely invalid file)");
            return nullptr;
        }

        // TODO: Allow storing 3-component RGB images. We do this for now to avoid handling it in runtime, because e.g. Vulkan doesn't always support sRGB8
        int desiredChannels = channelsInFile;
        if (channelsInFile == STBI_rgb) {
            desiredChannels = STBI_rgb_alpha;
        }

        void* data;
        size_t size;
        if (stbi_is_hdr_from_memory(sourceAssetData, static_cast<int>(sourceAssetSize))) {
            data = stbi_loadf_from_memory(sourceAssetData, static_cast<int>(sourceAssetSize), &width, &height, &channelsInFile, desiredChannels);
            size = width * height * desiredChannels * sizeof(float);
            isFloatType = true;
        } else {
            data = stbi_load_from_memory(sourceAssetData, static_cast<int>(sourceAssetSize), &width, &height, &channelsInFile, desiredChannels);
            size = width * height * desiredChannels * sizeof(stbi_uc);
        }

        auto format { ImageFormat::Unknown };
        #define SelectFormat(intFormat, floatFormat) (isFloatType ? floatFormat : intFormat)

        switch (desiredChannels) {
        case 1:
            format = SelectFormat(ImageFormat::R8, ImageFormat::R32F);
            break;
        case 2:
            format = SelectFormat(ImageFormat::RG8, ImageFormat::RG32F);
            break;
        case 3:
            format = SelectFormat(ImageFormat::RGB8, ImageFormat::RGB32F);
            break;
        case 4:
            format = SelectFormat(ImageFormat::RGBA8, ImageFormat::RGBA32F);
            break;
        }

        #undef SelectFormat
        ARKOSE_ASSERT(format != ImageFormat::Unknown);

        imageAsset = std::make_unique<ImageAsset>();

        imageAsset->m_extent = { narrow_cast<u32>(width), narrow_cast<u32>(height), 1 };

        imageAsset->m_format = format;
        imageAsset->m_type = ImageType::Unknown;

        uint8_t* dataPtr = reinterpret_cast<uint8_t*>(data);
        imageAsset->m_pixelData = std::vector<uint8_t>(dataPtr, dataPtr + size);

        ImageMip mip0 { .offset = 0,
                        .size = size };
        imageAsset->m_mips.push_back(mip0);

        if (data != nullptr) {
            stbi_image_free(data);
        }
    }

    return imageAsset;
}

std::unique_ptr<ImageAsset> ImageAsset::createFromRawData(uint8_t const* data, size_t size, ImageFormat format, Extent2D extent)
{
    SCOPED_PROFILE_ZONE();

    if (data == nullptr || size == 0) {
        return nullptr;
    }

    auto imageAsset = std::make_unique<ImageAsset>();

    imageAsset->m_extent = extent;

    imageAsset->m_format = format;
    imageAsset->m_type = ImageType::Unknown;

    imageAsset->m_pixelData = std::vector<uint8_t>(data, data + size);

    ImageMip mip0 { .offset = 0,
                    .size = size };
    imageAsset->m_mips.push_back(mip0);

    return imageAsset;
}

ImageAsset* ImageAsset::load(std::filesystem::path const& filePath)
{
    SCOPED_PROFILE_ZONE();

    if (not isValidAssetPath(filePath)) {
        ARKOSE_LOG(Warning, "Trying to load image asset with invalid file extension: '{}'", filePath);
    }

    if (ImageAsset* cachedAsset = s_imageAssetCache.get(filePath)) {
        return cachedAsset;
    }

    auto newImageAsset = std::make_unique<ImageAsset>();
    bool success = newImageAsset->readFromFile(filePath);

    if (!success) {
        return nullptr;
    }

    return s_imageAssetCache.put(filePath, std::move(newImageAsset));
}

ImageAsset* ImageAsset::manage(std::unique_ptr<ImageAsset>&& imageAsset)
{
    ARKOSE_ASSERT(!imageAsset->assetFilePath().empty());
    return s_imageAssetCache.put(imageAsset->assetFilePath(), std::move(imageAsset));
}

ImageAsset* ImageAsset::loadOrCreate(std::filesystem::path const& filePath)
{
    if (isValidAssetPath(filePath)) {
        return load(filePath);
    } else {

        if (ImageAsset* cachedAsset = s_imageAssetCache.get(filePath)) {
            return cachedAsset;
        }

        std::unique_ptr<ImageAsset> newImageAsset = createFromSourceAsset(filePath);
        if (not newImageAsset) {
            return nullptr;
        }

        newImageAsset->setAssetFilePath(filePath);

        return s_imageAssetCache.put(filePath, std::move(newImageAsset));
    }
}

bool ImageAsset::readFromFile(std::filesystem::path const& filePath)
{
    if (!isValidAssetPath(filePath)) {
        ARKOSE_LOG(Warning, "Trying to load image asset with invalid file extension: '{}'", filePath);
        return false;
    }

    std::optional<std::vector<std::byte>> maybeFileData = FileIO::readBinaryDataFromFile<std::byte>(filePath);
    if (!maybeFileData) {
        return false;
    }

    std::vector<std::byte> const& fileData = maybeFileData.value();
    if (!DDS::isValidHeader(reinterpret_cast<u8 const*>(fileData.data()), fileData.size())) { 
        ARKOSE_LOG(Warning, "File '{}' is not a valid DDS file, trying to load as image asset", filePath);
    }

    Extent3D extent;
    ImageFormat format;
    bool srgb;
    u32 numMips;
    void* data = (void*)DDS::loadFromMemory(reinterpret_cast<u8 const*>(fileData.data()), fileData.size(), extent, format, srgb, numMips);

    if (data == nullptr) {
        ARKOSE_LOG(Error, "Failed to load image asset '{}' (DDS reported error, likely invalid file)", filePath);
        return false;
    }

    m_extent = extent;

    m_format = format;
    m_type = srgb ? ImageType::sRGBColor : ImageType::Unknown;

    m_mips = DDS::computeMipOffsetAndSize(extent, format, numMips);

    uint8_t* dataPtr = reinterpret_cast<uint8_t*>(data);
    ARKOSE_ASSERT(m_mips[0].offset == 0); // we assume all the mips are laid out sequentially
    size_t dataSize = m_mips.back().offset + m_mips.back().size;
    m_pixelData = std::vector<uint8_t>(dataPtr, dataPtr + dataSize);

    setAssetFilePath(filePath);

    return true;
}

bool ImageAsset::writeToFile(std::filesystem::path const& filePath, AssetStorage assetStorage) const
{
    if (assetStorage != AssetStorage::Binary) {
        ARKOSE_LOG(Fatal, "Image asset only supports binary serialization.");
    }

    if (!isValidAssetPath(filePath)) {
        ARKOSE_LOG(Error, "Trying to write image asset to file with invalid extension: '{}'", filePath);
        return false;
    }

    bool sRGB = m_type == ImageType::sRGBColor;

    return DDS::writeToFile(filePath, m_pixelData.data(), m_pixelData.size(), m_extent, m_format, sRGB, narrow_cast<u32>(m_mips.size()));
}

std::span<u8 const> ImageAsset::pixelDataForMip(size_t mipIdx) const
{
    if (mipIdx >= m_mips.size()) {
        return {};
    }

    ImageMip const& mip = m_mips[mipIdx];
    ARKOSE_ASSERT(mip.size > 0);
    ARKOSE_ASSERT(mip.offset < m_pixelData.size());
    ARKOSE_ASSERT(mip.offset + mip.size <= m_pixelData.size());

    return std::span<u8 const> { m_pixelData.data() + mip.offset, mip.size };
}

size_t ImageAsset::totalImageSizeIncludingMips() const
{
    ARKOSE_ASSERT(m_mips.size() > 0);
    ImageMip const& lastMip = m_mips.back();
    return lastMip.offset + lastMip.size;
}

bool ImageAsset::generateMipmaps()
{
    SCOPED_PROFILE_ZONE();

    // TODO: Implement proper error handling!
    ARKOSE_ASSERT(m_mips.size() == 1);
    ARKOSE_ASSERT(depth() == 1);

    if (!ark::isPowerOfTwo(width()) || ark::isPowerOfTwo(height())) {
        // TODO: Implement non-power-of-two mipmap generation
        return false;
    }

    if (width() != height()) {
        // TODO: Implement non-square texture mipmap generation
        return false;
    }

    // TODO: Support more formats!
    ARKOSE_ASSERT(m_format == ImageFormat::RGBA8);

    u32 mipHeight = height();
    u32 mipWidth = width();
    u32 levels = static_cast<u32>(std::floor(std::log2(std::max(mipHeight, mipWidth))) + 1);

    for (u32 level = 1; level < levels; ++level) {

        std::string zoneName = fmt::format("Mip level {}", level);
        SCOPED_PROFILE_ZONE_DYNAMIC(zoneName, 0xaa5577);

        u32 previousMipLevel = level - 1;
        ImageMip previousMip = m_mips[previousMipLevel];
        std::vector<rgba8> previousMipPixels = pixelDataAsRGBA8(level - 1);

        ImageMip& thisMip = m_mips.emplace_back();
        thisMip.offset = previousMip.offset + previousMip.size;
        thisMip.size = previousMip.size / 4; // half size per 2D side

        m_pixelData.reserve(thisMip.offset + thisMip.size);

        u32 thisMipHeight = std::max(mipHeight / 2, 1u);
        u32 thisMipWidth = std::max(mipWidth / 2, 1u);

        for (u32 y = 0; y < thisMipHeight; ++y) {
            for (u32 x = 0; x < thisMipWidth; ++x) {

                u32 x0 = 2 * x + 0;
                u32 y0 = 2 * y + 0;
                u32 x1 = std::min(2 * x + 1, mipWidth - 1);
                u32 y1 = std::min(2 * y + 1, mipHeight - 1);

                rgba8 pix0 = previousMipPixels[x0 + y0 * mipWidth];
                rgba8 pix1 = previousMipPixels[x0 + y1 * mipWidth];
                rgba8 pix2 = previousMipPixels[x1 + y0 * mipWidth];
                rgba8 pix3 = previousMipPixels[x1 + y1 * mipWidth];

                // TODO: Would be nice to have some kind of vector constructor that can take a non-narrowing type an argument
                float rAvg = (static_cast<float>(pix0.x) + static_cast<float>(pix1.x) + static_cast<float>(pix2.x) + static_cast<float>(pix3.x)) / 4.0f;
                float gAvg = (static_cast<float>(pix0.y) + static_cast<float>(pix1.y) + static_cast<float>(pix2.y) + static_cast<float>(pix3.y)) / 4.0f;
                float bAvg = (static_cast<float>(pix0.z) + static_cast<float>(pix1.z) + static_cast<float>(pix2.z) + static_cast<float>(pix3.z)) / 4.0f;
                float aAvg = (static_cast<float>(pix0.w) + static_cast<float>(pix1.w) + static_cast<float>(pix2.w) + static_cast<float>(pix3.w)) / 4.0f;

                u8 r = static_cast<u8>(std::round(rAvg));
                u8 g = static_cast<u8>(std::round(gAvg));
                u8 b = static_cast<u8>(std::round(bAvg));
                u8 a = static_cast<u8>(std::round(aAvg));

                // (This works nicely when we're in rgba8 and it's a u8 vector, but for all other cases it won't be this simple)
                m_pixelData.emplace_back(r);
                m_pixelData.emplace_back(g);
                m_pixelData.emplace_back(b);
                m_pixelData.emplace_back(a);

            }
        }

        // Next mip..
        mipHeight = thisMipHeight;
        mipWidth = thisMipWidth;

    }

    return true;
}

Extent3D ImageAsset::extentAtMip(size_t mipIdx) const
{
    ARKOSE_ASSERT(mipIdx < m_mips.size());

    if (mipIdx == 0) {
        return m_extent;
    }

    float p = std::pow(2.0f, static_cast<float>(mipIdx));
    u32 x = static_cast<u32>(std::floor(width() / p));
    u32 y = static_cast<u32>(std::floor(height() / p));
    u32 z = static_cast<u32>(std::floor(depth() / p));

    x = x > 1 ? x : 1;
    y = y > 1 ? y : 1;
    z = z > 1 ? z : 1;

    return { x, y, z };
}

bool ImageAsset::hasCompressedFormat() const
{
    return imageFormatIsBlockCompressed(format());
}

ImageAsset::rgba8 ImageAsset::getPixelAsRGBA8(u32 x, u32 y, u32 z, u32 mipIdx) const
{
    SCOPED_PROFILE_ZONE();

    // TODO: Support more formats! The function name only refers to the output format and should be able to convert
    ARKOSE_ASSERT(format() == ImageFormat::RGBA8);

    // TODO: Account for stride != width etc.?
    u32 pixelIdx = x + (y * width()) + z * (width() * height());
    u32 byteStartIdx = 4 * pixelIdx;

    std::span<const u8> rawMipData = pixelDataForMip(mipIdx);

    rgba8 pixel { rawMipData[byteStartIdx + 0],
                  rawMipData[byteStartIdx + 1],
                  rawMipData[byteStartIdx + 2],
                  rawMipData[byteStartIdx + 3] };

    return pixel;
}

std::vector<ImageAsset::rgba8> ImageAsset::pixelDataAsRGBA8(size_t mipIdx) const
{
    ARKOSE_ASSERT(depth() == 1);

    // TODO: Support more formats! The function name only refers to the output format and should be able to convert
    ARKOSE_ASSERT(format() == ImageFormat::RGBA8);

    ImageMip const& mip = m_mips[mipIdx];
    size_t numPixels = mip.size / (4 * sizeof(u8));

    std::vector<rgba8> rgbaPixelData {};
    rgbaPixelData.reserve(numPixels);

    for (size_t offset = mip.offset; offset < mip.offset + mip.size; offset += 4) {
        rgbaPixelData.emplace_back(m_pixelData[offset + 0],
                                   m_pixelData[offset + 1],
                                   m_pixelData[offset + 2],
                                   m_pixelData[offset + 3]);
    }

    return rgbaPixelData;
}
