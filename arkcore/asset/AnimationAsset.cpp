#include "AnimationAsset.h"

#include "asset/AssetCache.h"
#include "core/Logging.h"
#include "utility/FileIO.h"
#include "utility/Profiling.h"
#include <cereal/cereal.hpp>
#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>
#include <mutex>

namespace {
AssetCache<AnimationAsset> s_animationAssetCache {};
}

AnimationAsset::AnimationAsset() = default;
AnimationAsset::~AnimationAsset() = default;

AnimationAsset* AnimationAsset::load(std::filesystem::path const& filePath)
{
    SCOPED_PROFILE_ZONE();

    if (!isValidAssetPath(filePath)) {
        ARKOSE_LOG(Warning, "Trying to load animation asset with invalid file extension: '{}'", filePath);
    }

    return s_animationAssetCache.getOrCreate(filePath, [&]() {
        auto newAnimationAsset = std::make_unique<AnimationAsset>();
        if (newAnimationAsset->readFromFile(filePath)) {
            return newAnimationAsset;
        } else {
            return std::unique_ptr<AnimationAsset>();
        }
    });
}

AnimationAsset* AnimationAsset::manage(std::unique_ptr<AnimationAsset>&& animationAsset)
{
    ARKOSE_ASSERT(!animationAsset->assetFilePath().empty());
    return s_animationAssetCache.put(animationAsset->assetFilePath(), std::move(animationAsset));
}

bool AnimationAsset::readFromFile(std::filesystem::path const& filePath)
{
    std::ifstream fileStream(filePath, std::ios::binary);
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

    setAssetFilePath(filePath);

    if (name.empty()) {
        name = filePath.stem().string();
    }

    return true;
}

bool AnimationAsset::writeToFile(std::filesystem::path const& filePath, AssetStorage assetStorage) const
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
        archive(cereal::make_nvp("animation", *this));
    } break;
    }

    fileStream.close();
    return true;
}
