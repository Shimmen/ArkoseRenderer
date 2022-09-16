#pragma once

#include "asset/AssetHelpers.h"
#include <string>
#include <string_view>

// Generated flatbuffer code
#include "MaterialAsset_generated.h"

using BlendMode = Arkose::Asset::BlendMode;
using WrapMode = Arkose::Asset::WrapMode;
using WrapModes = Arkose::Asset::WrapModes;
using ImageFilter = Arkose::Asset::ImageFilter;

using MaterialInputRaw = Arkose::Asset::MaterialInput;
using MaterialInput = Arkose::Asset::MaterialInputT;

using MaterialAssetRaw = Arkose::Asset::MaterialAsset;
class MaterialAsset : public Arkose::Asset::MaterialAssetT {
public:
    MaterialAsset();
    ~MaterialAsset();

    // Load a material asset (cached) from an .arkmat file
    // TODO: Figure out how we want to return this! Basic type, e.g. MaterialAsset*, or something reference counted, e.g. shared_ptr or manual ref-count?
    static MaterialAsset* loadFromArkmat(std::string const& filePath);

    bool writeToArkmat(std::string_view filePath, AssetStorage);

    std::string_view assetFilePath() const { return m_assetFilePath; }

private:
    // Construct a material asset from a loaded flatbuffer material asset file
    MaterialAsset(Arkose::Asset::MaterialAsset const*, std::string filePath);

    std::string m_assetFilePath {};
};
