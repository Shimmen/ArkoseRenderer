#pragma once

#include "asset/AnimationAsset.h"
#include "asset/ImageAsset.h"
#include "asset/LevelAsset.h"
#include "asset/MaterialAsset.h"
#include "asset/MeshAsset.h"
#include "asset/SkeletonAsset.h"
#include <string_view>
#include <vector>

struct MeshInstance {
    MeshAsset* mesh { nullptr };
    Transform transform {};
};

struct ImportedCamera {
    std::string name {};
    Transform transform {};
    float verticalFieldOfView {};
    float zNear {};
    float zFar{};
};

struct ImportResult {
    std::vector<std::unique_ptr<ImageAsset>> images {};
    std::vector<std::unique_ptr<MaterialAsset>> materials {};
    std::vector<std::unique_ptr<MeshAsset>> meshes {};
    std::vector<std::unique_ptr<SkeletonAsset>> skeletons {};
    std::vector<std::unique_ptr<AnimationAsset>> animations {};

    std::vector<ImportedCamera> cameras {};

    std::vector<MeshInstance> meshInstances {};
};

struct AssetImporterOptions {
    // By default we keep png/jpeg/etc. in their source formats. Set this to true to import all images as asset types.
    bool alwaysMakeImageAsset { false };
    // Generate mipmaps when importing image assets? Only supported when making image assets
    bool generateMipmaps { false };
    // Compress images in BC5 format for normal maps and BC7 for all other textures.
    bool blockCompressImages { false };
    // Save imported meshes in textual format
    bool saveMeshesInTextualFormat { false };
};

class AssetImporter {
public:
    ImportResult importAsset(std::string_view assetFilePath, std::string_view targetDirectory, AssetImporterOptions = AssetImporterOptions());
    ImportResult importGltf(std::string_view gltfFilePath, std::string_view targetDirectory, AssetImporterOptions = AssetImporterOptions());

    std::unique_ptr<LevelAsset> importAsLevel(std::string_view assetFilePath, std::string_view targetDirectory, AssetImporterOptions = AssetImporterOptions());

};
