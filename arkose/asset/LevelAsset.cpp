#include "LevelAsset.h"

#include "core/Assert.h"
#include "core/Logging.h"
#include "utility/FileIO.h"
#include "utility/Profiling.h"
#include <mutex>

namespace {
static std::mutex s_levelAssetCacheMutex {};
static std::unordered_map<std::string, std::unique_ptr<LevelAsset>> s_levelAssetCache {};
static std::unique_ptr<flatbuffers::Parser> s_levelAssetParser {};
}

LevelAsset::LevelAsset() = default;
LevelAsset::~LevelAsset() = default;

LevelAsset::LevelAsset(Arkose::Asset::LevelAsset const* flatbuffersLevelAsset, std::string filePath)
    : m_assetFilePath(std::move(filePath))
{
    ARKOSE_ASSERT(flatbuffersLevelAsset != nullptr);
    flatbuffersLevelAsset->UnPackTo(this);

    ARKOSE_ASSERT(m_assetFilePath.length() > 0);
}

LevelAsset* LevelAsset::loadFromArklvl(std::string const& filePath)
{
    SCOPED_PROFILE_ZONE();

    if (not AssetHelpers::isValidAssetPath(filePath, Arkose::Asset::LevelAssetExtension())) {
        ARKOSE_LOG(Warning, "Trying to load level asset with invalid file extension: '{}'", filePath);
    }

    {
        SCOPED_PROFILE_ZONE_NAMED("Level cache - load");
        std::scoped_lock<std::mutex> lock { s_levelAssetCacheMutex };

        auto entry = s_levelAssetCache.find(filePath);
        if (entry != s_levelAssetCache.end()) {
            return entry->second.get();
        }
    }

    auto maybeBinaryData = FileIO::readBinaryDataFromFile<uint8_t>(filePath);
    if (not maybeBinaryData.has_value()) {
        return nullptr;
    }

    void* binaryData = maybeBinaryData.value().data();

    bool isValidBinaryBuffer = Arkose::Asset::LevelAssetBufferHasIdentifier(binaryData);
    if (not isValidBinaryBuffer) {

        // See if we can parse it as json

        // First check: do we at least start with a '{' character?
        if (maybeBinaryData.value().size() == 0 || *reinterpret_cast<const char*>(binaryData) != '{') {
            return nullptr;
        }

        std::string asciiData = FileIO::readEntireFile(filePath).value();

        if (not s_levelAssetParser) {
            s_levelAssetParser = AssetHelpers::createAssetRuntimeParser("LevelAsset.fbs");
        }

        if (not s_levelAssetParser->ParseJson(asciiData.c_str(), filePath.c_str())) {
            ARKOSE_LOG(Error, "Failed to parse json text for level asset:\n\t{}", s_levelAssetParser->error_);
            return nullptr;
        }

        // Use the now filled-in builder's buffer as the binary data input
        binaryData = s_levelAssetParser->builder_.GetBufferPointer();
    }

    auto const* flatbuffersLevelAsset = Arkose::Asset::GetLevelAsset(binaryData);
    if (!flatbuffersLevelAsset) {
        return nullptr;
    }

    auto newLevelAsset = std::unique_ptr<LevelAsset>(new LevelAsset(flatbuffersLevelAsset, filePath));

    {
        SCOPED_PROFILE_ZONE_NAMED("Level cache - store");
        std::scoped_lock<std::mutex> lock { s_levelAssetCacheMutex };
        s_levelAssetCache[filePath] = std::move(newLevelAsset);
        return s_levelAssetCache[filePath].get();
    }
}

bool LevelAsset::writeToArklvl(std::string_view filePath, AssetStorage assetStorage)
{
    SCOPED_PROFILE_ZONE();

    if (not AssetHelpers::isValidAssetPath(filePath, Arkose::Asset::LevelAssetExtension())) {
        ARKOSE_LOG(Error, "Trying to write level asset to file with invalid extension: '{}'", filePath);
        return false;
    }

    ARKOSE_ASSERT(m_assetFilePath.empty() || m_assetFilePath == filePath);
    m_assetFilePath = filePath;

    flatbuffers::FlatBufferBuilder builder {};
    auto asset = Arkose::Asset::LevelAsset::Pack(builder, this);

    if (asset.IsNull()) {
        return false;
    }

    builder.Finish(asset, Arkose::Asset::LevelAssetIdentifier());

    uint8_t* data = builder.GetBufferPointer();
    size_t size = static_cast<size_t>(builder.GetSize());

    switch (assetStorage) {
    case AssetStorage::Binary:
        FileIO::writeBinaryDataToFile(std::string(filePath), data, size);
        break;
    case AssetStorage::Json: {

        if (not s_levelAssetParser) {
            s_levelAssetParser = AssetHelpers::createAssetRuntimeParser("LevelAsset.fbs");
        }

        std::string jsonText;
        if (not flatbuffers::GenerateText(*s_levelAssetParser, data, &jsonText)) {
            ARKOSE_LOG(Error, "Failed to generate json text for level asset");
            return false;
        }

        FileIO::writeTextDataToFile(std::string(filePath), jsonText);

    } break;
    }

    return true;
}
