#include "AssetImporter.h"

#include "asset/TextureCompressor.h"
#include "asset/import/GltfLoader.h"
#include "core/Assert.h"
#include "core/Logging.h"
#include "core/parallel/ParallelFor.h"
#include "utility/FileIO.h"

ImportResult AssetImporter::importAsset(std::string_view assetFilePath, std::string_view targetDirectory, Options options)
{
    SCOPED_PROFILE_ZONE();

    if (not FileIO::isFileReadable(std::string(assetFilePath))) {
        ARKOSE_LOG(Error, "Trying to import asset '{}' that is not readable / doesn't exist.", assetFilePath);
        return {};
    }

    if (assetFilePath.ends_with(".gltf") || assetFilePath.ends_with(".glb")) {
        return importGltf(assetFilePath, targetDirectory, options);
    }

    ARKOSE_LOG(Error, "Trying to import asset '{}' of unsupported file type.", assetFilePath);
    return ImportResult();
}

ImportResult AssetImporter::importGltf(std::string_view gltfFilePath, std::string_view targetDirectory, Options options)
{
    FileIO::ensureDirectory(std::string(targetDirectory));
    if (targetDirectory.ends_with('/')) {
        targetDirectory.remove_suffix(1);
    }

    if (options.blockCompressImages || options.generateMipmaps) {
        options.alwaysMakeImageAsset = true;
    }

    GltfLoader gltfLoader {};
    ImportResult result = gltfLoader.load(std::string(gltfFilePath));

    // Compress all images (the slow part of this process) in parallel
    ParallelFor(result.images.size(), [&](size_t idx) {
        auto& image = result.images[idx];

        // Only compress if we're importing images in arkimg format
        if (image->sourceAssetFilePath().empty() || options.alwaysMakeImageAsset) {

            if (options.generateMipmaps) {
                image->generateMipmaps();
            }

            if (options.blockCompressImages) {
                TextureCompressor textureCompressor {};
                if (image->type() == ImageType::NormalMap) {
                    image = textureCompressor.compressBC5(*image);
                } else {
                    image = textureCompressor.compressBC7(*image);
                }
            }

            image->compress();
        }
    });

    int unnamedImageIdx = 0;
    for (auto& image : result.images) {

        if (not image->hasSourceAsset() || options.alwaysMakeImageAsset) {

            std::string fileName;
            if (image->hasSourceAsset()) {
                fileName = std::string(FileIO::extractFileNameFromPath(image->sourceAssetFilePath()));
                fileName = std::string(FileIO::removeExtensionFromPath(fileName));
            } else {
                fileName = std::format("image{:04}", unnamedImageIdx++);
            }

            std::string targetFilePath = std::format("{}/{}.arkimg", targetDirectory, fileName);

            image->writeToFile(targetFilePath, AssetStorage::Binary);
        }
    }

    std::unordered_map<std::string, int> materialNameMap {};
    for (auto& material : result.materials) {

        // Resolve references (paths) to image assets
        // (The glTF loader will use its local glTF indices while loading, since we don't yet know the file paths)

        auto resolveImageFilePath = [&](std::optional<MaterialInput>& materialInput) {
            if (materialInput.has_value()) {
                int gltfIdx = materialInput->userData;
                ARKOSE_ASSERT(gltfIdx >= 0 && gltfIdx < result.images.size());
                auto& image = result.images[gltfIdx];
                std::string_view imagePath = (not image->hasSourceAsset() || options.alwaysMakeImageAsset)
                    ? image->assetFilePath()
                    : image->sourceAssetFilePath();
                materialInput->setPathToImage(std::string(imagePath));
            }
        };

        resolveImageFilePath(material->baseColor);
        resolveImageFilePath(material->emissiveColor);
        resolveImageFilePath(material->normalMap);
        resolveImageFilePath(material->materialProperties);

        std::string fileName = material->name;
        if (fileName.empty()) {
            fileName = "material";
        }

        int count = materialNameMap[fileName]++;
        if (count > 0 || fileName == "material") {
            fileName = std::format("{}_{}", fileName, count);
        }

        std::string targetFilePath = std::format("{}/{}.arkmat", targetDirectory, fileName);

        material->writeToFile(targetFilePath, AssetStorage::Json);
    }

    std::unordered_map<std::string, int> meshNameMap {};
    for (auto& mesh : result.meshes) {

        // Resolve references (paths) to material assets
        // (The glTF loader will use its local glTF indices while loading, since we don't yet know the file paths)

        for (MeshLODAsset& lod : mesh->LODs) {
            for (MeshSegmentAsset& meshSegment : lod.meshSegments) {
                if (meshSegment.userData != -1) {
                    int gltfIdx = meshSegment.userData;
                    ARKOSE_ASSERT(gltfIdx >= 0 && gltfIdx < result.materials.size());
                    auto& material = result.materials[gltfIdx];
                    meshSegment.setPathToMaterial(std::string(material->assetFilePath()));
                }
            }
        }

        std::string fileName = mesh->name;
        if (fileName.empty()) {
            fileName = "mesh";
        }

        int count = meshNameMap[fileName]++;
        if (count > 0 || fileName == "mesh") {
            fileName = std::format("{}_{}", fileName, count);
        }

        std::string targetFilePath = std::format("{}/{}.arkmsh", targetDirectory, fileName);

        // TODO: Write to json when importing! It's currently super slow with all the data we have, but if we separate out the core data it will be fine.
        mesh->writeToFile(targetFilePath, AssetStorage::Binary);
    }

    std::unordered_map<std::string, int> skeletonNameMap {};
    for (auto& skeleton : result.skeletons) {

        std::string fileName = skeleton->name;
        if (fileName.empty()) {
            fileName = "skeleton";
        }

        int count = skeletonNameMap[fileName]++;
        if (count > 0 || fileName == "skeleton") {
            fileName = std::format("{}_{}", fileName, count);
        }

        std::string targetFilePath = std::format("{}/{}.arkskel", targetDirectory, fileName);

        skeleton->writeToFile(targetFilePath, AssetStorage::Json);
    }

    std::unordered_map<std::string, int> animationNameMap {};
    for (auto& animation : result.animations) {

        std::string fileName = animation->name;
        if (fileName.empty()) {
            fileName = "animation";
        }

        int count = animationNameMap[fileName]++;
        if (count > 0 || fileName == "animation") {
            fileName = std::format("{}_{}", fileName, count);
        }

        std::string targetFilePath = std::format("{}/{}.arkanim", targetDirectory, fileName);

        animation->writeToFile(targetFilePath, AssetStorage::Json);

    }

    return result;
}

std::unique_ptr<LevelAsset> AssetImporter::importAsLevel(std::string_view assetFilePath, std::string_view targetDirectory, Options options)
{
    ImportResult result = importAsset(assetFilePath, targetDirectory, options);

    auto levelAsset = std::make_unique<LevelAsset>();

    // TODO: Also add lights, cameras, etc.

    for (MeshInstance const& meshInstance : result.meshInstances) {
        SceneObjectAsset sceneObject {};
        sceneObject.transform = meshInstance.transform;
        sceneObject.mesh = std::string(meshInstance.mesh->assetFilePath());
        levelAsset->objects.push_back(sceneObject);
    }

    for (ImportedCamera const& importedCamera : result.cameras) {
        Camera& camera = levelAsset->cameras.emplace_back();
        camera.setPosition(importedCamera.transform.positionInWorld());
        camera.setOrientation(importedCamera.transform.orientationInWorld());
        camera.setNearAndFarPlanes(importedCamera.zNear, importedCamera.zFar);
        camera.setFieldOfView(importedCamera.verticalFieldOfView);
    }

    std::string_view levelName = FileIO::removeExtensionFromPath(FileIO::extractFileNameFromPath(assetFilePath));
    levelAsset->name = std::string(levelName);

    std::string levelFilePath = std::format("{}{}.arklvl", targetDirectory, levelName);
    if (not levelAsset->writeToFile(levelFilePath, AssetStorage::Json)) {
        ARKOSE_LOG(Error, "Failed to write level asset '{}' to file.", levelAsset->name);
        return nullptr;
    }

    return levelAsset;
}
