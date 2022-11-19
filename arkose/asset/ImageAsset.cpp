#include "ImageAsset.h"

#include "core/Assert.h"
#include "core/Logging.h"
#include "utility/FileIO.h"
#include "utility/Profiling.h"
#include <cereal/cereal.hpp>
#include <cereal/archives/binary.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include <fstream>
#include <mutex>
#include <stb_image.h>
#include <zstd/zstd.h>

namespace {
    static std::mutex s_imageAssetCacheMutex {};
    static std::unordered_map<std::string, std::unique_ptr<ImageAsset>> s_imageAssetCache {};
}

ImageAsset::ImageAsset() = default;
ImageAsset::~ImageAsset() = default;

std::unique_ptr<ImageAsset> ImageAsset::createCopyWithReplacedFormat(ImageAsset const& inputImage, ImageFormat newFormat, std::vector<u8>&& pixelData, std::vector<ImageMip> imageMips)
{
    auto newImage = std::make_unique<ImageAsset>();

    newImage->m_width = inputImage.m_width;
    newImage->m_height = inputImage.m_height;
    newImage->m_depth = inputImage.m_depth;
    newImage->m_type = inputImage.m_type;
    newImage->m_sourceAssetFilePath = inputImage.m_sourceAssetFilePath;

    newImage->m_format = newFormat;
    newImage->m_pixelData = std::move(pixelData);
    newImage->m_mips = std::move(imageMips);

    // Passed in data must be in an uncompressed state (compressed data formats are okay but not lossless compression on pixel_data!)
    newImage->m_compressed = false;
    newImage->m_uncompressedSize = narrow_cast<u32>(newImage->m_pixelData.size());
    newImage->m_compressedSize = newImage->m_compressedSize;

    // TODO: Do we need to copy this over?
    newImage->userData = inputImage.userData;

    return newImage;
}

std::unique_ptr<ImageAsset> ImageAsset::createFromSourceAsset(std::string const& sourceAssetFilePath)
{
    SCOPED_PROFILE_ZONE();

    auto maybeData = FileIO::readBinaryDataFromFile<uint8_t>(sourceAssetFilePath);
    if (not maybeData.has_value()) {
        return nullptr;
    }

    uint8_t* data = maybeData.value().data();
    size_t size = maybeData.value().size();

    auto imageAsset = createFromSourceAsset(data, size);
    imageAsset->m_sourceAssetFilePath = sourceAssetFilePath;

    return imageAsset;
}

std::unique_ptr<ImageAsset> ImageAsset::createFromSourceAsset(uint8_t const* sourceAssetData, size_t sourceAssetSize)
{
    SCOPED_PROFILE_ZONE();

    void* data;
    size_t size;

    bool isFloatType = false;
    int width, height, channelsInFile;

    int success = stbi_info_from_memory(sourceAssetData, static_cast<int>(sourceAssetSize), &width, &height, &channelsInFile);

    if (not success) {
        ARKOSE_LOG(Error, "Failed to load to load image asset (stb reported error, likely invalid file)");
        return nullptr;
    }

    // TODO: Allow storing 3-component RGB images. We do this for now to avoid handling it in runtime, because e.g. Vulkan doesn't always support sRGB8
    int desiredChannels = channelsInFile;
    if (channelsInFile == STBI_rgb) {
        desiredChannels = STBI_rgb_alpha;
    }

    if (stbi_is_hdr_from_memory(sourceAssetData, static_cast<int>(sourceAssetSize))) {
        data = stbi_loadf_from_memory(sourceAssetData, static_cast<int>(sourceAssetSize), &width, &height, &channelsInFile, desiredChannels);
        size = width * height * desiredChannels * sizeof(float);
        isFloatType = true;
    } else {
        data = stbi_load_from_memory(sourceAssetData, static_cast<int>(sourceAssetSize), &width, &height, &channelsInFile, desiredChannels);
        size = width * height * desiredChannels * sizeof(stbi_uc);
    }

    auto format { ImageFormat::Unknown };
    #define SelectFormat(intFormat, floatFormat) (isFloatType ? ImageFormat::##floatFormat : ImageFormat::##intFormat)

    switch (desiredChannels) {
    case 1:
        format = SelectFormat(R8, R32F);
        break;
    case 2:
        format = SelectFormat(RG8, RG32F);
        break;
    case 3:
        format = SelectFormat(RGB8, RGB32F);
        break;
    case 4:
        format = SelectFormat(RGBA8, RGBA32F);
        break;
    }

    #undef SelectFormat
    ARKOSE_ASSERT(format != ImageFormat::Unknown);

    auto imageAsset = std::make_unique<ImageAsset>();
    
    imageAsset->m_width = width;
    imageAsset->m_height = height;
    imageAsset->m_depth = 1;

    imageAsset->m_format = format;
    imageAsset->m_type = ImageType::Unknown;

    uint8_t* dataPtr = reinterpret_cast<uint8_t*>(data);
    imageAsset->m_pixelData = std::vector<uint8_t>(dataPtr, dataPtr + size);

    ImageMip mip0 { .offset = 0,
                    .size = size };
    imageAsset->m_mips.push_back(mip0);

    // No compression when creating here now, but we might want to apply it before writing to disk
    imageAsset->m_compressed = false;
    imageAsset->m_uncompressedSize = narrow_cast<u32>(size);

    if (data != nullptr) {
        stbi_image_free(data);
    }

    return imageAsset;
}

ImageAsset* ImageAsset::loadFromArkimg(std::string const& filePath)
{
    SCOPED_PROFILE_ZONE();

    if (not AssetHelpers::isValidAssetPath(filePath, ImageAsset::AssetFileExtension)) {
        ARKOSE_LOG(Warning, "Trying to load image asset with invalid file extension: '{}'", filePath);
    }

    {
        SCOPED_PROFILE_ZONE_NAMED("Image cache - load");
        std::scoped_lock<std::mutex> lock { s_imageAssetCacheMutex };

        auto entry = s_imageAssetCache.find(filePath);
        if (entry != s_imageAssetCache.end()) {
            return entry->second.get();
        }
    }

    std::ifstream fileStream(filePath, std::ios::binary);
    if (not fileStream.is_open()) {
        return nullptr;
    }

    cereal::BinaryInputArchive archive(fileStream);

    AssetHeader header;
    archive(header);

    if (header != AssetHeader(AssetMagicValue)) {
        ARKOSE_LOG(Warning, "Trying to load image asset with invalid file magic: '{}'", fmt::join(header.magicValue, ""));
        return nullptr;
    }

    auto newImageAsset = std::make_unique<ImageAsset>();
    archive(*newImageAsset);

    newImageAsset->m_assetFilePath = filePath;

    if (newImageAsset->isCompressed()) {
        newImageAsset->decompress();
    }

    {
        SCOPED_PROFILE_ZONE_NAMED("Image cache - store");
        std::scoped_lock<std::mutex> lock { s_imageAssetCacheMutex };
        s_imageAssetCache[filePath] = std::move(newImageAsset);
        return s_imageAssetCache[filePath].get();
    }
}

ImageAsset* ImageAsset::loadOrCreate(std::string const& filePath)
{
    if (AssetHelpers::isValidAssetPath(filePath, ImageAsset::AssetFileExtension)) {
        return loadFromArkimg(filePath);
    } else {

        {
            SCOPED_PROFILE_ZONE_NAMED("Image cache - load source asset");
            std::scoped_lock<std::mutex> lock { s_imageAssetCacheMutex };

            auto entry = s_imageAssetCache.find(filePath);
            if (entry != s_imageAssetCache.end()) {
                return entry->second.get();
            }
        }

        std::unique_ptr<ImageAsset> newImageAsset = createFromSourceAsset(filePath);
        if (not newImageAsset) {
            return nullptr;
        }

        newImageAsset->m_assetFilePath = filePath;

        {
            SCOPED_PROFILE_ZONE_NAMED("Image cache - store source asset");
            std::scoped_lock<std::mutex> lock { s_imageAssetCacheMutex };
            s_imageAssetCache[filePath] = std::move(newImageAsset);
            return s_imageAssetCache[filePath].get();
        }
    }
}

bool ImageAsset::writeToArkimg(std::string_view filePath)
{
    if (not AssetHelpers::isValidAssetPath(filePath, ImageAsset::AssetFileExtension)) {
        ARKOSE_LOG(Error, "Trying to write image asset to file with invalid extension: '{}'", filePath);
        return false;
    }

    ARKOSE_ASSERT(m_assetFilePath.empty() || m_assetFilePath == filePath);
    m_assetFilePath = filePath;

    if (not m_compressed) {
        compress();
    }

    std::ofstream fileStream { m_assetFilePath, std::ios::binary | std::ios::trunc };
    if (not fileStream.is_open()) {
        return false;
    }

    cereal::BinaryOutputArchive archive(fileStream);

    archive(AssetHeader(AssetMagicValue));
    archive(*this);

    fileStream.close();
    return true;
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
    ARKOSE_ASSERT(width() == height());
    ARKOSE_ASSERT(ark::isPowerOfTwo(width()));
    ARKOSE_ASSERT(ark::isPowerOfTwo(height()));
    ARKOSE_ASSERT(depth() == 1);

    // TODO: Support more formats!
    ARKOSE_ASSERT(m_format == ImageFormat::RGBA8);

    u32 mipWidth = width(); // should be identical to height
    u32 levels = static_cast<u32>(std::floor(std::log2(mipWidth)) + 1);

    for (u32 level = 1; level < levels; ++level) {

        std::string zoneName = fmt::format("Mip level {}", level);
        SCOPED_PROFILE_ZONE_DYNAMIC(zoneName, 0xaa5577);

        ImageMip previousMip = m_mips[level - 1];
        std::vector<rgba8> previousMipPixels = pixelDataAsRGBA8(level - 1);

        ImageMip& thisMip = m_mips.emplace_back();
        thisMip.offset = previousMip.offset + previousMip.size;
        thisMip.size = previousMip.size / 4; // half size per 2D side

        m_pixelData.reserve(thisMip.offset + thisMip.size);

        u32 thisMipWidth = mipWidth / 2;

        // TODO: Parallelize!
        for (u32 y = 0; y < thisMipWidth; ++y) {
            for (u32 x = 0; x < thisMipWidth; ++x) {

                rgba8 pix0 = previousMipPixels[(2 * x + 0) + (2 * y + 0) * mipWidth];
                rgba8 pix1 = previousMipPixels[(2 * x + 0) + (2 * y + 1) * mipWidth];
                rgba8 pix2 = previousMipPixels[(2 * x + 1) + (2 * y + 0) * mipWidth];
                rgba8 pix3 = previousMipPixels[(2 * x + 1) + (2 * y + 1) * mipWidth];

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
        mipWidth = thisMipWidth;

    }

    return true;
}

Extent3D ImageAsset::extentAtMip(size_t mipIdx) const
{
    ARKOSE_ASSERT(mipIdx < m_mips.size());

    if (mipIdx == 0) {
        return { width(), height(), depth() };
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
    switch (format()) {
    //case ImageFormat::BC5: TODO!
    case ImageFormat::BC7:
        return true;
    default:
        return false;
    }
}

bool ImageAsset::compress(int compressionLevel)
{
    SCOPED_PROFILE_ZONE();

    if (m_compressed) {
        return true;
    }

    size_t maxCompressedSize = ZSTD_compressBound(m_pixelData.size());
    std::vector<uint8_t> compressedBlob {};
    compressedBlob.resize(maxCompressedSize);

    size_t compressedSizeOrErrorCode = ZSTD_compress(compressedBlob.data(), compressedBlob.size(), m_pixelData.data(), m_pixelData.size(), compressionLevel);

    if (ZSTD_isError(compressedSizeOrErrorCode)) {
        const char* errorName = ZSTD_getErrorName(compressedSizeOrErrorCode);
        ARKOSE_LOG(Error, "Failed to compress image. Reason: {}", errorName);
        return false;
    }

    size_t compressedSize = compressedSizeOrErrorCode;
    compressedBlob.resize(compressedSize);

    m_uncompressedSize =  narrow_cast<u32>(m_pixelData.size());
    m_compressedSize = narrow_cast<u32>(compressedSize);
    std::swap(m_pixelData, compressedBlob);
    m_compressed = true;

    return true;
}

bool ImageAsset::decompress()
{
    SCOPED_PROFILE_ZONE();

    if (not m_compressed) {
        return true;
    }

    ARKOSE_ASSERT(m_uncompressedSize > 0);
    std::vector<uint8_t> decompressedData {};
    decompressedData.resize(m_uncompressedSize);

    size_t decompressedSizeOrErrorCode = ZSTD_decompress(decompressedData.data(), decompressedData.size(),
                                                         m_pixelData.data(), m_pixelData.size());

    if (ZSTD_isError(decompressedSizeOrErrorCode)) {
        const char* errorName = ZSTD_getErrorName(decompressedSizeOrErrorCode);
        ARKOSE_LOG(Error, "Failed to decompress image. Reason: {}", errorName);
        return false;
    }

    size_t decompressedSize = decompressedSizeOrErrorCode;
    if (decompressedSize != m_uncompressedSize) {
        ARKOSE_LOG(Error, "Decompressed size does not match uncompressed size in image asset: {} vs {}", decompressedSize, m_uncompressedSize);
        return false;
    }

    std::swap(m_pixelData, decompressedData);

    m_compressed = false;
    m_compressedSize = 0;

    return true;
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
