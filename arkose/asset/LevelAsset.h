#pragma once

#include "asset/Asset.h"
#include "scene/ProbeGrid.h"
#include "scene/EnvironmentMap.h"
#include "scene/Scene.h"
#include "scene/SceneObject.h"
#include "scene/camera/Camera.h"
#include "scene/lights/Light.h"
#include <string>
#include <string_view>
#include <variant>
#include <vector>

class LevelAsset final : public Asset<LevelAsset> {
public:
    LevelAsset();
    ~LevelAsset();

    static constexpr const char* AssetFileExtension = "arklvl";
    static constexpr std::array<char, 4> AssetMagicValue = { 'a', 'l', 'v', 'l' };

    // Load a level asset (cached) from an .arklvl file
    // TODO: Figure out how we want to return this! Basic type, e.g. LevelAsset*, or something reference counted, e.g. shared_ptr or manual ref-count?
    static LevelAsset* loadFromArklvl(std::string const& filePath);

    virtual bool readFromFile(std::string_view filePath) override;
    virtual bool writeToFile(std::string_view filePath, AssetStorage assetStorage) override;

    template<class Archive>
    void serialize(Archive&);

    // All objects in this level
    std::vector<SceneObject> objects;

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

#include <cereal/types/optional.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/string.hpp>

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
