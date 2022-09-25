#pragma once

#include "asset/AssetHelpers.h"
#include <string>
#include <string_view>

// Generated flatbuffer code
#include "LevelAsset_generated.h"

using ProbeGridAsset = Arkose::Asset::ProbeGridT;

using EnvironmentMapAsset = Arkose::Asset::EnvironmentMapT;

using CameraAsset = Arkose::Asset::CameraT;

using SceneObjectAsset = Arkose::Asset::SceneObjectT;

using LightAsset = Arkose::Asset::LightUnion;
using DirectionalLightAsset = Arkose::Asset::DirectionalLightT;
using SpotLightAsset = Arkose::Asset::SpotLightT;

using LevelAssetRaw = Arkose::Asset::LevelAsset;
class LevelAsset : public Arkose::Asset::LevelAssetT {
public:
    LevelAsset();
    ~LevelAsset();

    // Load a level asset (cached) from an .arklvl file
    // TODO: Figure out how we want to return this! Basic type, e.g. LevelAsset*, or something reference counted, e.g. shared_ptr or manual ref-count?
    static LevelAsset* loadFromArklvl(std::string const& filePath);

    bool writeToArklvl(std::string_view filePath, AssetStorage);

    std::string_view assetFilePath() const { return m_assetFilePath; }

private:
    // Construct a material asset from a loaded flatbuffer material asset file
    LevelAsset(Arkose::Asset::LevelAsset const*, std::string filePath);

    std::string m_assetFilePath {};
};
