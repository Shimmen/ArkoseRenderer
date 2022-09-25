#include "ImageAsset.h"

#include "core/Assert.h"
#include "core/Logging.h"
#include "utility/FileIO.h"
#include "utility/Profiling.h"
#include <mutex>
#include <stb_image.h>
#include <zstd/zstd.h>

namespace {
    static std::mutex s_imageAssetCacheMutex {};
    static std::unordered_map<std::string, std::unique_ptr<ImageAsset>> s_imageAssetCache {};
}

ImageAsset::ImageAsset() = default;
ImageAsset::~ImageAsset() = default;

ImageAsset::ImageAsset(Arkose::Asset::ImageAsset const* flatbuffersImageAsset, std::string filePath)
    : m_assetFilePath(std::move(filePath))
{
    ARKOSE_ASSERT(flatbuffersImageAsset != nullptr);
    flatbuffersImageAsset->UnPackTo(this);

    ARKOSE_ASSERT(m_assetFilePath.length() > 0);
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
    imageAsset->source_asset_path = sourceAssetFilePath;

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

    auto format { Arkose::Asset::ImageFormat::Unknown };
    #define SelectFormat(intFormat, floatFormat) (isFloatType ? Arkose::Asset::ImageFormat::##floatFormat : Arkose::Asset::ImageFormat::##intFormat)

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
    ARKOSE_ASSERT(format != Arkose::Asset::ImageFormat::Unknown);

    auto imageAsset = std::make_unique<ImageAsset>();
    
    imageAsset->width = width;
    imageAsset->height = height;
    imageAsset->depth = 1;

    imageAsset->format = format;

    // Let's make a safe assumption, the user can change it later if needed
    imageAsset->color_space = Arkose::Asset::ColorSpace::sRGB_encoded;

    uint8_t* dataPtr = reinterpret_cast<uint8_t*>(data);
    imageAsset->pixel_data = std::vector<uint8_t>(dataPtr, dataPtr + size);

    // No compression when creating here now, but we might want to apply it before writing to disk
    imageAsset->is_compressed = false;
    imageAsset->uncompressed_size = static_cast<uint32_t>(size);

    if (data != nullptr) {
        stbi_image_free(data);
    }

    return imageAsset;
}

ImageAsset* ImageAsset::loadFromArkimg(std::string const& filePath)
{
    SCOPED_PROFILE_ZONE();

    if (not AssetHelpers::isValidAssetPath(filePath, Arkose::Asset::ImageAssetExtension())) {
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

    auto maybeBinaryData = FileIO::readBinaryDataFromFile<uint8_t>(filePath);
    if (not maybeBinaryData.has_value()) {
        return nullptr;
    }

    void* binaryData = maybeBinaryData.value().data();
    auto const* flatbuffersImageAsset = Arkose::Asset::GetImageAsset(binaryData);

    if (!flatbuffersImageAsset) {
        return nullptr;
    }

    auto newImageAsset = std::unique_ptr<ImageAsset>(new ImageAsset(flatbuffersImageAsset, filePath));

    if (newImageAsset->is_compressed) {
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
    if (AssetHelpers::isValidAssetPath(filePath, Arkose::Asset::ImageAssetExtension())) {
        return loadFromArkimg(filePath);
    } else {
        std::unique_ptr<ImageAsset> newImageAsset = createFromSourceAsset(filePath);
        {
            SCOPED_PROFILE_ZONE_NAMED("Image cache - store source asset");
            std::scoped_lock<std::mutex> lock { s_imageAssetCacheMutex };
            s_imageAssetCache[filePath] = std::move(newImageAsset);
            return s_imageAssetCache[filePath].get();
        }
    }
}

StreamedImageAsset ImageAsset::loadForStreaming(std::string const& filePath)
{
    if (not AssetHelpers::isValidAssetPath(filePath, Arkose::Asset::ImageAssetExtension())) {
        ARKOSE_LOG(Error, "Trying to write image asset to file with invalid extension: '{}'", filePath);
        return StreamedImageAsset();
    }

    // TODO: Could we maybe just mmap it? Would that be quicker?
    size_t dataSize = 0;
    uint8_t* binaryData = FileIO::readBinaryDataFromFileRawPtr(filePath, &dataSize);

    auto const* imageAsset = Arkose::Asset::GetImageAsset(binaryData);

    return StreamedImageAsset(binaryData, imageAsset);
}

bool ImageAsset::writeToArkimg(std::string_view filePath)
{
    if (not AssetHelpers::isValidAssetPath(filePath, Arkose::Asset::ImageAssetExtension())) {
        ARKOSE_LOG(Error, "Trying to write image asset to file with invalid extension: '{}'", filePath);
        return false;
    }

    ARKOSE_ASSERT(m_assetFilePath.empty() || m_assetFilePath == filePath);
    m_assetFilePath = filePath;

    if (not is_compressed) {
        compress();
    }

    flatbuffers::FlatBufferBuilder builder {};
    auto asset = Arkose::Asset::ImageAsset::Pack(builder, this);

    if (asset.IsNull()) {
        return false;
    }

    builder.Finish(asset, Arkose::Asset::ImageAssetIdentifier());

    uint8_t* data = builder.GetBufferPointer();
    size_t size = static_cast<size_t>(builder.GetSize());

    FileIO::writeBinaryDataToFile(std::string(filePath), data, size);

    return true;
}

bool ImageAsset::compress(int compressionLevel)
{
    SCOPED_PROFILE_ZONE();

    if (is_compressed) {
        return true;
    }

    size_t maxCompressedSize = ZSTD_compressBound(pixel_data.size());
    std::vector<uint8_t> compressedBlob {};
    compressedBlob.resize(maxCompressedSize);

    size_t compressedSizeOrErrorCode = ZSTD_compress(compressedBlob.data(), compressedBlob.size(), pixel_data.data(), pixel_data.size(), compressionLevel);

    if (ZSTD_isError(compressedSizeOrErrorCode)) {
        const char* errorName = ZSTD_getErrorName(compressedSizeOrErrorCode);
        ARKOSE_LOG(Error, "Failed to compress image. Reason: {}", errorName);
        return false;
    }

    size_t compressedSize = compressedSizeOrErrorCode;
    compressedBlob.resize(compressedSize);

    uncompressed_size = static_cast<uint32_t>(pixel_data.size());
    compressed_size = static_cast<uint32_t>(compressedSize);
    std::swap(pixel_data, compressedBlob);
    is_compressed = true;

    return true;
}

bool ImageAsset::decompress()
{
    SCOPED_PROFILE_ZONE();

    if (not is_compressed) {
        return true;
    }

    ARKOSE_ASSERT(uncompressed_size > 0);
    std::vector<uint8_t> decompressedData {};
    decompressedData.resize(uncompressed_size);

    size_t decompressedSizeOrErrorCode = ZSTD_decompress(decompressedData.data(), decompressedData.size(),
                                                         pixel_data.data(), pixel_data.size());

    if (ZSTD_isError(decompressedSizeOrErrorCode)) {
        const char* errorName = ZSTD_getErrorName(decompressedSizeOrErrorCode);
        ARKOSE_LOG(Error, "Failed to decompress image. Reason: {}", errorName);
        return false;
    }

    size_t decompressedSize = decompressedSizeOrErrorCode;
    if (decompressedSize != uncompressed_size) {
        ARKOSE_LOG(Error, "Decompressed size does not match uncompressed size in image asset: {} vs {}", decompressedSize, uncompressed_size);
        return false;
    }

    std::swap(pixel_data, decompressedData);

    is_compressed = false;
    compressed_size = static_cast<uint32_t>(0);

    return true;
}

StreamedImageAsset::StreamedImageAsset(uint8_t* memory, Arkose::Asset::ImageAsset const* asset)
    : m_memory(memory)
    , m_asset(asset)
{
}

StreamedImageAsset::StreamedImageAsset(StreamedImageAsset&& other) noexcept
    : m_memory(other.m_memory)
    , m_asset(other.m_asset)
{
    other.m_memory = nullptr;
    other.m_asset = nullptr;
    ARKOSE_ASSERT(other.isNull());
}

void StreamedImageAsset::unload()
{
    if (not isNull()) {
        m_asset = nullptr;

        free(m_memory);
        m_memory = nullptr;
    }
}
