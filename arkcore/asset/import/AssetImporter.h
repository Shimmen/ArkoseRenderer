#pragma once

#include "asset/AnimationAsset.h"
#include "asset/ImageAsset.h"
#include "asset/LevelAsset.h"
#include "asset/MaterialAsset.h"
#include "asset/MeshAsset.h"
#include "asset/SkeletonAsset.h"
#include "core/parallel/PollableTask.h"
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

    std::vector<std::unique_ptr<LightAsset>> lights {};
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


// All asset importing is wrapped into this pollable task, meaning it can be run async and polled for its status.
// If you wish to import synchronously, simply create an AssetImportTask and call `executeSynchronous()` on it. 
class AssetImportTask : public PollableTask {
public:
    static std::unique_ptr<AssetImportTask> create(std::string_view assetFilePath, std::string_view targetDirectory, AssetImporterOptions);

    bool success() const;
    ImportResult* result();

    virtual float progress() const override;
    virtual std::string status() const override;

private:
    AssetImportTask(std::string_view assetFilePath, std::string_view targetDirectory, AssetImporterOptions);

    void importAsset();
    void importGltf();

    std::string m_assetFilePath {};
    std::string m_targetDirectory {};
    AssetImporterOptions m_options {};

    ImportResult m_result {};

    bool m_error { false };
    char const* m_status = "Importing asset";

    size_t m_processedItemCount { 0 };
    size_t m_totalItemCount { 0 };
};
