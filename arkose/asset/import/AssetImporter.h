#pragma once

#include "asset/ImageAsset.h"
#include "asset/LevelAsset.h"
#include "asset/MaterialAsset.h"
#include "asset/StaticMeshAsset.h"
#include <string_view>
#include <vector>

struct MeshInstance {
    StaticMeshAsset* staticMesh { nullptr };
    Transform transform {};
};

struct ImportResult {
    std::vector<std::unique_ptr<ImageAsset>> images {};
    std::vector<std::unique_ptr<MaterialAsset>> materials {};
    std::vector<std::unique_ptr<StaticMeshAsset>> staticMeshes {};

    std::vector<MeshInstance> meshInstances {};
};

class AssetImporter {
public:

    struct Options {
        // By default we keep png/jpeg/etc. in their source formats. Set this to true to import all images as asset types.
        bool alwaysMakeImageAsset { false };
        // Generate mipmaps when importing image assets? Only supported when making image assets
        bool generateMipmaps { false };
        // Compress images in BC5 format for normal maps and BC7 for all other textures.
        bool blockCompressImages { false };
    };

    ImportResult importAsset(std::string_view assetFilePath, std::string_view targetDirectory, Options = Options());
    ImportResult importGltf(std::string_view gltfFilePath, std::string_view targetDirectory, Options = Options());

    std::unique_ptr<LevelAsset> importAsLevel(std::string_view assetFilePath, std::string_view targetDirectory, Options = Options());

};
