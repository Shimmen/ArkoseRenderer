#include "AssetImporter.h"

#include "asset/import/GltfLoader.h"
#include "core/Assert.h"
#include "core/Logging.h"
#include "core/parallel/ParallelFor.h"
#include "utility/FileIO.h"

ImportResult AssetImporter::importAsset(std::string_view assetFilePath, std::string_view targetDirectory)
{
    SCOPED_PROFILE_ZONE();

    if (not FileIO::isFileReadable(std::string(assetFilePath))) {
        ARKOSE_LOG(Error, "Trying to import asset '{}' that is not readable / doesn't exist.", assetFilePath);
        return {};
    }

    if (assetFilePath.ends_with(".gltf") || assetFilePath.ends_with(".glb")) {
        return importGltf(assetFilePath, targetDirectory);
    }

    ARKOSE_LOG(Error, "Trying to import asset '{}' of unsupported file type.", assetFilePath);
    return ImportResult();
}

static Arkose::Asset::PathT createAssetPath(std::string_view assetFilePath)
{
    Arkose::Asset::PathT path {};
    path.path = std::string(assetFilePath);

    return path;
}

ImportResult AssetImporter::importGltf(std::string_view gltfFilePath, std::string_view targetDirectory)
{
    FileIO::ensureDirectory(std::string(targetDirectory));

    GltfLoader_NEW gltfLoader {};
    ImportResult result = gltfLoader.load(std::string(gltfFilePath));

    // Compress all images (the slow part of this process) in parallel
    ParallelFor(result.images.size(), [&](size_t idx) {
        result.images[idx]->compress();
    });

    int unnamedImageIdx = 0;
    for (auto& image : result.images) {

        std::string fileName;
        if (not image->source_asset_path.empty()) {
            fileName = std::string(FileIO::extractFileNameFromPath(image->source_asset_path));
            fileName = std::string(FileIO::removeExtensionFromPath(fileName));
        } else {
            fileName = std::format("image{:04}", unnamedImageIdx++);
        }

        std::string targetFilePath = std::format("{}/{}.arkimg", targetDirectory, fileName);

        image->writeToArkimg(targetFilePath);
    }

    int unnamedMaterialIdx = 0;
    for (auto& material : result.materials) {

        // Resolve references (paths) to image assets
        // (The glTF loader will use its local glTF indices while loading, since we don't yet know the file paths)

        auto resolveImageFilePath = [&](Arkose::Asset::MaterialInputT* materialInput) {
            if (materialInput) {
                int gltfIdx = materialInput->user_data.integer();
                ARKOSE_ASSERT(gltfIdx >= 0 && gltfIdx < result.images.size());
                auto& image = result.images[gltfIdx];
                materialInput->image.Set(createAssetPath(image->assetFilePath()));
            }
        };

        resolveImageFilePath(material->base_color.get());
        resolveImageFilePath(material->emissive_color.get());
        resolveImageFilePath(material->normal_map.get());
        resolveImageFilePath(material->material_properties.get());

        std::string fileName = std::format("material{:04}", unnamedMaterialIdx++);
        std::string targetFilePath = std::format("{}/{}.arkmat", targetDirectory, fileName);

        // TODO: Write to json when importing!
        material->writeToArkmat(targetFilePath, AssetStorage::Binary);
    }

    int unnamedMeshIdx = 0;
    for (auto& staticMesh : result.staticMeshes) {

        // Resolve references (paths) to material assets
        // (The glTF loader will use its local glTF indices while loading, since we don't yet know the file paths)

        for (auto& lod : staticMesh->lods) {
            for (auto& meshSegment : lod->mesh_segments) {
                if (meshSegment->user_data.integer() != -1) {
                    int gltfIdx = meshSegment->user_data.integer();
                    ARKOSE_ASSERT(gltfIdx >= 0 && gltfIdx < result.materials.size());
                    auto& material = result.materials[gltfIdx];
                    meshSegment->material.Set(createAssetPath(material->assetFilePath()));
                }
            }
        }

        std::string fileName = std::format("staticmesh{:04}", unnamedMeshIdx++);
        std::string targetFilePath = std::format("{}/{}.arkmsh", targetDirectory, fileName);

        // TODO: Write to json when importing! It's currently super slow with all the data we have, but if we separate out the core data it will be fine.
        staticMesh->writeToArkmsh(targetFilePath, AssetStorage::Binary);
    }

    return result;
}
