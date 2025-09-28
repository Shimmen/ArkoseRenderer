#pragma once

#include "asset/Asset.h"
#include "scene/ProbeGrid.h"
#include "scene/EnvironmentMap.h"
#include "scene/Transform.h"
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

    std::string set;
};

class CameraAsset {
public:
    CameraAsset() = default;
    ~CameraAsset() = default;

    template<class Archive>
    void serialize(Archive&);

    vec3 position;
    quat orientation;

    float nearClipPlane = 0.25f;
    float farClipPlane = 10'000.0f;

    std::string focusMode = "Manual";
    float focalLength = 30.0f;
    float focusDepth = 5.0f;
    vec2 sensorSize = { 36.0f, 24.0f };

    std::string exposureMode = "Manual";
    float fNumber = 16.0f;
    float iso = 400.0f;
    float shutterSpeed = 1.0f / 400.0f;

    float exposureCompensation = 0.0f;
    float adaptionRate = 0.0018f;
};

class DirectionalLightAssetData {
public:
    DirectionalLightAssetData() = default;
    ~DirectionalLightAssetData() = default;

    template<class Archive>
    void serialize(Archive&);

    float illuminance;
    float shadowMapWorldExtent;
};

class SphereLightAssetData {
public:
    SphereLightAssetData() = default;
    ~SphereLightAssetData() = default;

    template<class Archive>
    void serialize(Archive&);

    float luminousPower;
    float lightRadius;
    float lightSourceRadius;
};

class SpotLightAssetData {
public:
    SpotLightAssetData() = default;
    ~SpotLightAssetData() = default;

    template<class Archive>
    void serialize(Archive&);

    std::string iesProfilePath;
    float luminousIntensity;
    float outerConeAngle;
};

class LightAsset {
public:
    LightAsset() = default;
    ~LightAsset() = default;

    template<class Archive>
    void serialize(Archive&);

    std::string type;
    std::string name;
    
    vec3 color;
    Transform transform;

    bool castsShadows;
    float customConstantBias;
    float customSlopeBias;

    std::variant<DirectionalLightAssetData,
                 SphereLightAssetData,
                 SpotLightAssetData> data;
};

class LevelAsset final : public Asset<LevelAsset> {
public:
    LevelAsset();
    ~LevelAsset();

    static constexpr const char* AssetFileExtension = ".arklvl";
    static constexpr std::array<char, 4> AssetMagicValue = { 'a', 'l', 'v', 'l' };

    // Load a level asset (cached) from an .arklvl file
    // TODO: Figure out how we want to return this! Basic type, e.g. LevelAsset*, or something reference counted, e.g. shared_ptr or manual ref-count?
    static LevelAsset* load(std::filesystem::path const& filePath);

    static std::unique_ptr<LevelAsset> createFromAssetImportResult(struct ImportResult const&);

    virtual bool readFromFile(std::filesystem::path const& filePath) override;
    virtual bool writeToFile(std::filesystem::path const& filePath, AssetStorage assetStorage) const override;

    template<class Archive>
    void serialize(Archive&);

    // All objects in this level
    std::vector<SceneObjectAsset> objects;

    // All lights in this level
    std::vector<LightAsset> lights;

    // List of predetermined cameras, of which the first one is the default
    std::vector<CameraAsset> cameras;

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
void DirectionalLightAssetData::serialize(Archive& archive)
{
    archive(cereal::make_nvp("illuminance", illuminance));
    archive(cereal::make_nvp("shadowMapWorldExtent", shadowMapWorldExtent));
}

template<class Archive>
void SphereLightAssetData::serialize(Archive& archive)
{
    archive(cereal::make_nvp("luminousPower", luminousPower));
    archive(cereal::make_nvp("lightRadius", lightRadius));
    archive(cereal::make_nvp("lightSourceRadius", lightSourceRadius));
}

template<class Archive>
void SpotLightAssetData::serialize(Archive& archive)
{
    archive(cereal::make_nvp("iesProfilePath", iesProfilePath));
    archive(cereal::make_nvp("luminousIntensity", luminousIntensity));
    archive(cereal::make_nvp("outerConeAngle", outerConeAngle));
}

template<class Archive>
void LightAsset::serialize(Archive& archive)
{
    archive(cereal::make_nvp("type", type));
    archive(cereal::make_nvp("name", name));

    archive(cereal::make_nvp("color", color));
    archive(cereal::make_nvp("transform", transform));

    archive(cereal::make_nvp("castsShadows", castsShadows));
    archive(cereal::make_nvp("customConstantBias", customConstantBias));
    archive(cereal::make_nvp("customSlopeBias", customSlopeBias));

    archive(cereal::make_nvp("data", data));
}

template<class Archive>
void CameraAsset::serialize(Archive& archive)
{
    archive(cereal::make_nvp("position", position));
    archive(cereal::make_nvp("orientation", orientation));

    archive(cereal::make_nvp("nearClipPlane", nearClipPlane));
    archive(cereal::make_nvp("farClipPlane", farClipPlane));

    archive(cereal::make_nvp("focusMode", focusMode));
    archive(cereal::make_nvp("focalLength", focalLength));
    archive(cereal::make_nvp("focusDepth", focusDepth));
    archive(cereal::make_nvp("sensorSize", sensorSize));

    archive(cereal::make_nvp("exposureMode", exposureMode));
    
    archive(cereal::make_nvp("fNumber", fNumber));
    archive(cereal::make_nvp("iso", iso));
    archive(cereal::make_nvp("shutterSpeed", shutterSpeed));

    archive(cereal::make_nvp("exposureCompensation", exposureCompensation));
    archive(cereal::make_nvp("adaptionRate", adaptionRate));
}

template<class Archive>
void SceneObjectAsset::serialize(Archive& archive)
{
    archive(cereal::make_nvp("name", name));
    archive(cereal::make_nvp("transform", transform));
    archive(cereal::make_nvp("mesh", mesh));
    archive(cereal::make_nvp("set", set));
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
