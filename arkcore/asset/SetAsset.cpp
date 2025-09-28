#include "SetAsset.h"

#include "asset/AssetCache.h"
#include "asset/import/AssetImporter.h"
#include "core/Assert.h"
#include "core/Logging.h"
#include "utility/FileIO.h"
#include "utility/Profiling.h"

namespace {
AssetCache<SetAsset> s_setAssetCache {};
}

NodeAsset* NodeAsset::createChildNode()
{
    std::unique_ptr<NodeAsset>& child = children.emplace_back(new NodeAsset());
    return child.get();
}

SetAsset::SetAsset() = default;
SetAsset::~SetAsset() = default;

SetAsset* SetAsset::load(std::filesystem::path const& filePath)
{
    SCOPED_PROFILE_ZONE();

    if (!isValidAssetPath(filePath)) {
        ARKOSE_LOG(Warning, "Trying to load set asset with invalid file extension: '{}'", filePath);
    }

    return s_setAssetCache.getOrCreate(filePath, [&]() {
        auto newSetAsset = std::make_unique<SetAsset>();
        if (newSetAsset->readFromFile(filePath)) {
            return newSetAsset;
        } else {
            return std::unique_ptr<SetAsset>();
        }
    });
}

bool SetAsset::readFromFile(std::filesystem::path const& filePath)
{
    std::ifstream fileStream(filePath, std::ios::binary);
    if (!fileStream.is_open()) {
        ARKOSE_LOG(Error, "Failed to load set asset at path '{}'", filePath);
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

    setAssetFilePath(filePath);

    if (name.empty()) {
        name = filePath.stem().string();
    }

    return true;
}

bool SetAsset::writeToFile(std::filesystem::path const& filePath, AssetStorage assetStorage) const
{
    if (!isValidAssetPath(filePath)) {
        ARKOSE_LOG(Error, "Trying to write asset to file with invalid extension: '{}'", filePath);
        return false;
    }

    std::ofstream fileStream { filePath, std::ios::binary | std::ios::trunc };
    if (!fileStream.is_open()) {
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
        archive(cereal::make_nvp("set", *this));
    } break;
    }

    fileStream.close();
    return true;
}
