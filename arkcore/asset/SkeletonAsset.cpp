#include "SkeletonAsset.h"

#include "asset/AssetCache.h"

namespace {
AssetCache<SkeletonAsset> s_skeletonAssetCache {};
}

SkeletonJointAsset::SkeletonJointAsset() = default;
SkeletonJointAsset::~SkeletonJointAsset() = default;

SkeletonAsset::SkeletonAsset() = default;
SkeletonAsset::~SkeletonAsset() = default;

SkeletonAsset* SkeletonAsset::load(std::filesystem::path const& filePath)
{
    SCOPED_PROFILE_ZONE();

    if (!isValidAssetPath(filePath)) {
        ARKOSE_LOG(Warning, "Trying to load skeleton asset with invalid file extension: '{}'", filePath);
    }

    return s_skeletonAssetCache.getOrCreate(filePath, [&]() {
        auto newSkeletonAsset = std::make_unique<SkeletonAsset>();
        if (newSkeletonAsset->readFromFile(filePath)) {
            return newSkeletonAsset;
        } else {
            return std::unique_ptr<SkeletonAsset>();
        }
    });
}

SkeletonAsset* SkeletonAsset::manage(std::unique_ptr<SkeletonAsset>&& skeletonAsset)
{
    ARKOSE_ASSERT(!skeletonAsset->assetFilePath().empty());
    return s_skeletonAssetCache.put(skeletonAsset->assetFilePath(), std::move(skeletonAsset));
}

bool SkeletonAsset::readFromFile(std::filesystem::path const& filePath)
{
    std::ifstream fileStream(filePath, std::ios::binary);
    if (!fileStream.is_open()) {
        ARKOSE_LOG(Error, "Failed to load skeleton asset at path '{}'", filePath);
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

bool SkeletonAsset::writeToFile(std::filesystem::path const& filePath, AssetStorage assetStorage) const
{
    if (not isValidAssetPath(filePath)) {
        ARKOSE_LOG(Error, "Trying to write asset to file with invalid extension: '{}'", filePath);
        return false;
    }

    std::ofstream fileStream { filePath, std::ios::binary | std::ios::trunc };
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
