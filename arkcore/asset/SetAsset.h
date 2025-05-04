#pragma once

#include "asset/Asset.h"
#include "scene/Transform.h"
#include <memory>
#include <string>
#include <vector>

class NodeAsset {
public:
    NodeAsset() = default;
    ~NodeAsset() = default;

    NodeAsset* createChildNode();

    template<class Archive>
    void serialize(Archive&, u32 version);

    std::string name {};
    Transform transform {};

    static constexpr i32 InvalidIndex = -1;

    i32 meshIndex { InvalidIndex };
    i32 lightIndex { InvalidIndex };
    i32 cameraIndex { InvalidIndex };

    std::vector<std::unique_ptr<NodeAsset>> children {};
};

class SetAsset final : public Asset<SetAsset> {
public:
    SetAsset();
    ~SetAsset();

    static constexpr const char* AssetFileExtension = ".arkset";
    static constexpr std::array<char, 4> AssetMagicValue = { 'a', 's', 'e', 't' };

    // Load a set asset (cached) from an .arkset file
    // TODO: Figure out how we want to return this! Basic type, e.g. SetAsset*, or something reference counted, e.g. shared_ptr or manual ref-count?
    static SetAsset* load(std::filesystem::path const& filePath);

    virtual bool readFromFile(std::filesystem::path const& filePath) override;
    virtual bool writeToFile(std::filesystem::path const& filePath, AssetStorage assetStorage) const override;

    template<class Archive>
    void serialize(Archive&, u32 version);

    NodeAsset rootNode {};

    std::vector<std::string> meshAssets {};
    //std::vector<LightAsset> lightAssets {};
    //std::vector<CameraAsset> cameraAssets {};
};

////////////////////////////////////////////////////////////////////////////////
// Serialization

#include <cereal/cereal.hpp>
#include <cereal/types/memory.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>

enum class SetAssetVersion : u32 {
    Initial = 0,
    ////////////////////////////////////////////////////////////////////////////
    // Add new versions above this delimiter
    VersionCount,
    LatestVersion = VersionCount - 1
};

CEREAL_CLASS_VERSION(NodeAsset, magic_enum::enum_integer(SetAssetVersion::LatestVersion))
CEREAL_CLASS_VERSION(SetAsset, magic_enum::enum_integer(SetAssetVersion::LatestVersion))

template<class Archive>
void NodeAsset::serialize(Archive& archive, u32 version)
{
    archive(cereal::make_nvp("name", name));
    archive(cereal::make_nvp("transform", transform));

    archive(cereal::make_nvp("meshIndex", meshIndex));
    archive(cereal::make_nvp("lightIndex", lightIndex));
    archive(cereal::make_nvp("cameraIndex", cameraIndex));

    archive(cereal::make_nvp("children", children));
}

template<class Archive>
void SetAsset::serialize(Archive& archive, u32 version)
{
    archive(cereal::make_nvp("name", name));
    archive(cereal::make_nvp("rootNode", rootNode));

    archive(cereal::make_nvp("meshAssets", meshAssets));
}
