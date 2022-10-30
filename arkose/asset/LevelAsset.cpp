#include "LevelAsset.h"

#include "core/Assert.h"
#include "core/Logging.h"
#include "utility/FileIO.h"
#include "utility/Profiling.h"
#include <mutex>

namespace {
static std::mutex s_levelAssetCacheMutex {};
static std::unordered_map<std::string, std::unique_ptr<LevelAsset>> s_levelAssetCache {};
}

LevelAsset::LevelAsset() = default;
LevelAsset::~LevelAsset() = default;

LevelAsset* LevelAsset::loadFromArklvl(std::string const& filePath)
{
    SCOPED_PROFILE_ZONE();

    if (not AssetHelpers::isValidAssetPath(filePath, LevelAsset::AssetFileExtension)) {
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

    std::ifstream fileStream(filePath, std::ios::binary);
    if (not fileStream.is_open()) {
        return nullptr;
    }

    std::unique_ptr<LevelAsset> newLevelAsset {};

    cereal::BinaryInputArchive binaryArchive(fileStream);

    AssetHeader header;
    binaryArchive(header);

    if (header == AssetHeader(AssetMagicValue)) {

        newLevelAsset = std::make_unique<LevelAsset>();
        binaryArchive(*newLevelAsset);

    } else {

        fileStream.seekg(0); // seek back to the start

        if (static_cast<char>(fileStream.peek()) != '{') {
            ARKOSE_LOG(Error, "Failed to parse json text for level asset '{}'", filePath);
            return nullptr;
        }

        cereal::JSONInputArchive jsonArchive(fileStream);

        newLevelAsset = std::make_unique<LevelAsset>();
        jsonArchive(*newLevelAsset);
    }

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

    if (not AssetHelpers::isValidAssetPath(filePath, LevelAsset::AssetFileExtension)) {
        ARKOSE_LOG(Error, "Trying to write level asset to file with invalid extension: '{}'", filePath);
        return false;
    }

    ARKOSE_ASSERT(m_assetFilePath.empty() || m_assetFilePath == filePath);
    m_assetFilePath = filePath;

    std::ofstream fileStream { m_assetFilePath, std::ios::binary | std::ios::trunc };
    if (not fileStream.is_open()) {
        return false;
    }

    switch (assetStorage) {
    case AssetStorage::Binary: {
        cereal::BinaryOutputArchive archive(fileStream);
        archive(AssetHeader(AssetMagicValue));
        archive(*this);
    } break;
    case AssetStorage::Json: {
        cereal::JSONOutputArchive archive(fileStream);
        archive(cereal::make_nvp("level", *this));
    } break;
    }

    fileStream.close();
    return true;
}
