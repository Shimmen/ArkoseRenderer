#pragma once

#include "asset/Asset.h"
#include "core/Types.h"
#include "scene/Transform.h"
#include <string>

class SkeletonJointAsset {
public:
    SkeletonJointAsset();
    ~SkeletonJointAsset();

    template<class Archive>
    void serialize(Archive&, u32 version);

    std::string name {}; // for referencing by name
    u32 index {}; // for referencing from vertex by index

    Transform transform {};
    mat4 invBindMatrix {};
    std::vector<SkeletonJointAsset> children {};
};

class SkeletonAsset final : public Asset<SkeletonAsset> {
public:
    SkeletonAsset();
    ~SkeletonAsset();

    static constexpr const char* AssetFileExtension = ".arkskel";
    static constexpr std::array<char, 4> AssetMagicValue = { 'a', 's', 'k', 'l' };

    // Load a skeleton asset (cached) from an .arkskel file
    // TODO: Figure out how we want to return this! Basic type, e.g. SkeletonAsset*, or something reference counted, e.g. shared_ptr or manual ref-count?
    static SkeletonAsset* load(std::filesystem::path const& filePath);

    static SkeletonAsset* manage(std::unique_ptr<SkeletonAsset>&&);

    virtual bool readFromFile(std::filesystem::path const& filePath) override;
    virtual bool writeToFile(std::filesystem::path const& filePath, AssetStorage assetStorage) const override;

    template<class Archive>
    void serialize(Archive&, u32 version);

    SkeletonJointAsset rootJoint {};
    u32 maxJointIdx { 0 };

};

////////////////////////////////////////////////////////////////////////////////
// Serialization

#include "asset/SerialisationHelpers.h"
#include "utility/EnumHelpers.h"
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>

enum class SkeletonAssetVersion : u32 {
    Initial = 0,
    ////////////////////////////////////////////////////////////////////////////
    // Add new versions above this delimiter
    LatestVersion
};

CEREAL_CLASS_VERSION(SkeletonAsset, toUnderlying(SkeletonAssetVersion::LatestVersion))

template<class Archive>
void SkeletonJointAsset::serialize(Archive& archive, u32 versionInt)
{
    archive(cereal::make_nvp("name", name));
    archive(cereal::make_nvp("index", index));
    archive(cereal::make_nvp("transform", transform));
    archive(cereal::make_nvp("invBindMatrix", invBindMatrix));
    archive(cereal::make_nvp("children", children));
}

template<class Archive>
void SkeletonAsset::serialize(Archive& archive, u32 versionInt)
{
    archive(cereal::make_nvp("name", name));
    archive(cereal::make_nvp("rootJoint", rootJoint));
    archive(cereal::make_nvp("maxJointIdx", maxJointIdx));
}
