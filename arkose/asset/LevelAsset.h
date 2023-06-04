#pragma once

#include "asset/Asset.h"
#include "scene/ProbeGrid.h"
#include "scene/EnvironmentMap.h"
#include "scene/Scene.h"
#include "scene/camera/Camera.h"
#include "scene/lights/Light.h"
#include <string>
#include <string_view>
#include <variant>
#include <vector>

class SceneObjectAsset {
public:
    SceneObjectAsset() = default;
    ~SceneObjectAsset() = default;

    template<class Archive>
    void serialize(Archive&);

    bool hasPathToMesh() const { return std::holds_alternative<std::string>(mesh); }
    std::string_view pathToMesh() const
    {
        ARKOSE_ASSERT(hasPathToMesh());
        return std::get<std::string>(mesh);
    }

    std::string name {};
    Transform transform {};

    // Path to a mesh or an mesh asset directly
    // TODO: Convert static mesh asset!
    // std::variant<std::string, std::weak_ptr<MeshAsset>> mesh;
    std::variant<std::string, int> mesh;
};

class LevelAsset final : public Asset<LevelAsset> {
public:
    LevelAsset();
    ~LevelAsset();

    static constexpr const char* AssetFileExtension = "arklvl";
    static constexpr std::array<char, 4> AssetMagicValue = { 'a', 'l', 'v', 'l' };

    // Load a level asset (cached) from an .arklvl file
    // TODO: Figure out how we want to return this! Basic type, e.g. LevelAsset*, or something reference counted, e.g. shared_ptr or manual ref-count?
    static LevelAsset* load(std::string const& filePath);

    virtual bool readFromFile(std::string_view filePath) override;
    virtual bool writeToFile(std::string_view filePath, AssetStorage assetStorage) override;

    template<class Archive>
    void serialize(Archive&);

    // All objects in this level
    std::vector<SceneObjectAsset> objects;

    // All lights in this level
    std::vector<std::unique_ptr<Light>> lights;

    // List of predetermined cameras, of which the first one is the default
    std::vector<Camera> cameras;

    // Environment map, used for skybox etc.
    std::optional<EnvironmentMap> environmentMap;

    // For use with spatial probe grid based algorithms such as DDGI
    std::optional<ProbeGrid> probeGrid;
};

////////////////////////////////////////////////////////////////////////////////
// Serialization

#include <cereal/cereal.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/variant.hpp>

template<class Archive>
void SceneObjectAsset::serialize(Archive& archive)
{
    archive(cereal::make_nvp("name", name));
    archive(cereal::make_nvp("transform", transform));
    archive(cereal::make_nvp("mesh", mesh));
}

template<class Archive>
void LevelAsset::serialize(Archive& archive)
{
    archive(cereal::make_nvp("name", name));
    archive(cereal::make_nvp("objects", objects));
    archive(cereal::make_nvp("lights", lights));
    archive(cereal::make_nvp("cameras", cameras));
    archive(cereal::make_nvp("environmentMap", environmentMap));
    archive(cereal::make_nvp("probeGrid", probeGrid));
}
