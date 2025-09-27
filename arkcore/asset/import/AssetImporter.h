#pragma once

#include "asset/AnimationAsset.h"
#include "asset/ImageAsset.h"
#include "asset/LevelAsset.h"
#include "asset/MaterialAsset.h"
#include "asset/MeshAsset.h"
#include "asset/SetAsset.h"
#include "asset/SkeletonAsset.h"
#include "asset/misc/ImageBakeSpec.h"
#include "core/parallel/PollableTask.h"
#include <atomic>
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
    std::vector<std::unique_ptr<ImageBakeSpec>> imageSpecs {};

    std::vector<std::unique_ptr<MaterialAsset>> materials {};
    std::vector<std::unique_ptr<MeshAsset>> meshes {};
    std::vector<std::unique_ptr<SkeletonAsset>> skeletons {};
    std::vector<std::unique_ptr<AnimationAsset>> animations {};

    std::unique_ptr<SetAsset> set {};

    std::vector<std::unique_ptr<LightAsset>> lights {};
    std::vector<ImportedCamera> cameras {};

    std::vector<MeshInstance> meshInstances {};
};

struct AssetImporterOptions {
    // Generate mipmaps when importing image assets?
    bool generateMipmaps { false };
    // Compress images in BC5 format for normal maps and BC7 for all other textures.
    bool blockCompressImages { false };
    // Generate iamge specs instead of image assets (so they can be procesed separately)
    bool generateImageSpecs { false };
    // Save imported meshes in textual format
    bool saveMeshesInTextualFormat { false };
};


// All asset importing is wrapped into this pollable task, meaning it can be run async and polled for its status.
// If you wish to import synchronously, simply create an AssetImportTask and call `executeSynchronous()` on it. 
class AssetImportTask : public PollableTask {
public:
    static std::unique_ptr<AssetImportTask> create(std::filesystem::path const& assetFilePath,
                                                   std::filesystem::path const& targetDirectory,
                                                   std::filesystem::path const& tempDirectory,
                                                   AssetImporterOptions);

    bool success() const;
    ImportResult* result();

    virtual float progress() const override;
    virtual std::string status() const override;

private:
    AssetImportTask(std::filesystem::path const& assetFilePath,
                    std::filesystem::path const& targetDirectory,
                    std::filesystem::path const& tempDirectory,
                    AssetImporterOptions);

    void importAsset();
    void importGltf();

    std::filesystem::path m_assetFilePath {};
    std::filesystem::path m_targetDirectory {};
    std::filesystem::path m_tempDirectory {};
    AssetImporterOptions m_options {};

    ImportResult m_result {};

    bool m_error { false };
    char const* m_status = "Importing asset";

    std::atomic_uint64_t m_processedItemCount { 0 };
    size_t m_totalItemCount { 0 };
};
