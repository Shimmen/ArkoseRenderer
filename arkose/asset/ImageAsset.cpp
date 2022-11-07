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

std::unique_ptr<ImageAsset> ImageAsset::createCopyWithReplacedFormat(ImageAsset const& inputImage, ImageFormat newFormat, uint8_t const* newData, size_t newSize)
{
    auto newImage = std::make_unique<ImageAsset>();

    newImage->m_width = inputImage.m_width;
    newImage->m_height = inputImage.m_height;
    newImage->m_depth = inputImage.m_depth;
    newImage->m_colorSpace = inputImage.m_colorSpace;
    newImage->m_sourceAssetFilePath = inputImage.m_sourceAssetFilePath;

    newImage->m_format = newFormat;
    newImage->m_pixelData.assign(newData, newData + newSize);

    // TODO: Handle multiple mips in this function!
    newImage->m_mips = std::vector<ImageMip> { ImageMip { .offset = 0, .size = newSize } };

    // Passed in data must be in an uncompressed state (compressed data formats are okay but not lossless compression on pixel_data!)
    newImage->m_compressed = false;
    newImage->m_compressedSize = narrow_cast<u32>(newSize);
    newImage->m_uncompressedSize = narrow_cast<u32>(newSize);

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

    // Let's make a safe assumption, the user can change it later if needed
    imageAsset->m_colorSpace = ColorSpace::sRGB_encoded;

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
