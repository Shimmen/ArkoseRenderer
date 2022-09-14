#pragma once

#include "asset/AssetHelpers.h"
#include <string>
#include <string_view>

// Generated flatbuffer code
#include "StaticMeshAsset_generated.h"

using StaticMeshSegmentAsset = Arkose::Asset::StaticMeshSegmentT;
using StaticMeshLODAsset = Arkose::Asset::StaticMeshLODT;

class StaticMeshAsset : public Arkose::Asset::StaticMeshAssetT {
public:
    StaticMeshAsset();
    ~StaticMeshAsset();

    // Load a static mesh asset (cached) from an .arkmsh file
    // TODO: Figure out how we want to return this! Basic type, e.g. StaticMeshAsset*, or something reference counted, e.g. shared_ptr or manual ref-count?
    static StaticMeshAsset* loadFromArkmsh(std::string const& filePath);

    bool writeToArkmsh(std::string_view filePath);

private:
    // Construct a static mesh asset from a loaded flatbuffer material asset file
    StaticMeshAsset(Arkose::Asset::StaticMeshAsset const*, std::string filePath);

    std::string m_assetFilePath {};
};
