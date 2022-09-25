#pragma once

#include "asset/ImageAsset.h"
#include "asset/MaterialAsset.h"
#include "asset/StaticMeshAsset.h"
#include <string_view>
#include <vector>

struct ImportResult {
    std::vector<std::unique_ptr<ImageAsset>> images {};
    std::vector<std::unique_ptr<MaterialAsset>> materials {};
    std::vector<std::unique_ptr<StaticMeshAsset>> staticMeshes {};
};

class AssetImporter {
public:

    struct Options {
        // By default we keep png/jpeg/etc. in their source formats. Set this to true to import all images as asset types.
        bool alwaysMakeImageAsset { false };
    };

    ImportResult importAsset(std::string_view assetFilePath, std::string_view targetDirectory, Options = Options());
    ImportResult importGltf(std::string_view gltfFilePath, std::string_view targetDirectory, Options = Options());

};
