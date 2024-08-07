#include "SkeletonAsset.h"

#include "asset/AssetCache.h"

namespace {
AssetCache<SkeletonAsset> s_skeletonAssetCache {};
}

SkeletonJointAsset::SkeletonJointAsset() = default;
SkeletonJointAsset::~SkeletonJointAsset() = default;

SkeletonAsset::SkeletonAsset() = default;
SkeletonAsset::~SkeletonAsset() = default;

SkeletonAsset* SkeletonAsset::load(std::string const& filePath)
{
    SCOPED_PROFILE_ZONE();

    if (not isValidAssetPath(filePath)) {
        ARKOSE_LOG(Warning, "Trying to load skeleton asset with invalid file extension: '{}'", filePath);
    }

    if (SkeletonAsset* cachedAsset = s_skeletonAssetCache.get(filePath)) {
        return cachedAsset;
    }

    auto newSkeletonAsset = std::make_unique<SkeletonAsset>();
    bool success = newSkeletonAsset->readFromFile(filePath);

    if (!success) {
        return nullptr;
    }

    return s_skeletonAssetCache.put(filePath, std::move(newSkeletonAsset));
}

bool SkeletonAsset::readFromFile(std::string_view filePath)
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

bool SkeletonAsset::writeToFile(std::string_view filePath, AssetStorage assetStorage) const
{
    if (not isValidAssetPath(filePath)) {
        ARKOSE_LOG(Error, "Trying to write asset to file with invalid extension: '{}'", filePath);
        return false;
    }

    std::ofstream fileStream { std::string(filePath), std::ios::binary | std::ios::trunc };
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
        archive(cereal::make_nvp("skeleton", *this));
    } break;
    }

    fileStream.close();
    return true;
}
