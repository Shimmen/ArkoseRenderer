#include "LevelAsset.h"

#include "asset/AssetCache.h"
#include "core/Assert.h"
#include "core/Logging.h"
#include "utility/FileIO.h"
#include "utility/Profiling.h"

namespace {
AssetCache<LevelAsset> s_levelAssetCache {};
}

LevelAsset::LevelAsset() = default;
LevelAsset::~LevelAsset() = default;

LevelAsset* LevelAsset::load(std::string const& filePath)
{
    SCOPED_PROFILE_ZONE();

    if (not isValidAssetPath(filePath)) {
        ARKOSE_LOG(Warning, "Trying to load level asset with invalid file extension: '{}'", filePath);
    }

    if (LevelAsset* cachedAsset = s_levelAssetCache.get(filePath)) {
        return cachedAsset;
    }

    auto newLevelAsset = std::make_unique<LevelAsset>();
    bool success = newLevelAsset->readFromFile(filePath);

    if (!success) {
        return nullptr;
    }

    return s_levelAssetCache.put(filePath, std::move(newLevelAsset));
}

bool LevelAsset::readFromFile(std::string_view filePath)
{
    std::ifstream fileStream(std::string(filePath), std::ios::binary);
    if (not fileStream.is_open()) {
        return false;
    }

    cereal::BinaryInputArchive binaryArchive(fileStream);

    AssetHeader header;
    binaryArchive(header);

    if (header == AssetHeader(AssetMagicValue)) {

        binaryArchive(*this);

    } else {

        fileStream.seekg(0); // seek back to the start

        if (static_cast<char>(fileStream.peek()) != '{') {
            ARKOSE_LOG(Error, "Failed to parse json text for asset '{}'", filePath);
            return false;
        }

        cereal::JSONInputArchive jsonArchive(fileStream);
        jsonArchive(*this);
    }

    this->setAssetFilePath(filePath);

    if (name.empty()) {
        this->name = FileIO::removeExtensionFromPath(FileIO::extractFileNameFromPath(filePath));
    }

    return true;
}

bool LevelAsset::writeToFile(std::string_view filePath, AssetStorage assetStorage)
{
    if (not isValidAssetPath(filePath)) {
        ARKOSE_LOG(Error, "Trying to write asset to file with invalid extension: '{}'", filePath);
        return false;
    }

    ARKOSE_ASSERT(assetFilePath().empty() || assetFilePath() == filePath);
    setAssetFilePath(filePath);

    std::ofstream fileStream { std::string(assetFilePath()), std::ios::binary | std::ios::trunc };
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
