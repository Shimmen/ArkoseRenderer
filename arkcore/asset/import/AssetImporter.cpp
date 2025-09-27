#include "AssetImporter.h"

#include "asset/TextureCompressor.h"
#include "asset/SetAsset.h"
#include "asset/import/GltfLoader.h"
#include "asset/misc/ImageBakeSpec.h"
#include "core/Assert.h"
#include "core/Logging.h"
#include "core/parallel/ParallelFor.h"
#include "utility/FileIO.h"
#include <fmt/format.h>

std::unique_ptr<AssetImportTask> AssetImportTask::create(std::filesystem::path const& assetFilePath,
                                                         std::filesystem::path const& targetDirectory,
                                                         std::filesystem::path const& tempDirectory,
                                                         AssetImporterOptions options)
{
    // NOTE: The task auto-release logic be damned..
    return std::unique_ptr<AssetImportTask>(new AssetImportTask(assetFilePath, targetDirectory, tempDirectory, options));
}

AssetImportTask::AssetImportTask(std::filesystem::path const& assetFilePath,
                                 std::filesystem::path const& targetDirectory,
                                 std::filesystem::path const& tempDirectory,
                                 AssetImporterOptions options)
    : PollableTask([this]() { importAsset(); })
    , m_assetFilePath(assetFilePath)
    , m_targetDirectory(targetDirectory)
    , m_tempDirectory(tempDirectory)
    , m_options(options)
{
    FileIO::ensureDirectory(m_targetDirectory);
    FileIO::ensureDirectory(m_tempDirectory);
}

bool AssetImportTask::success() const
{
    return m_error == false;
}

ImportResult* AssetImportTask::result()
{
    if (progress() >= 1.0f) {
        ARKOSE_ASSERT(isCompleted());
        return &m_result;
    } else {
        ARKOSE_ERROR("AssetImportTask::result(): not yet available");
        return nullptr;
    }
}

float AssetImportTask::progress() const
{
    if (m_totalItemCount == 0) {
        return 0.0f;
    }

    return static_cast<float>(m_processedItemCount) / static_cast<float>(m_totalItemCount);
}

std::string AssetImportTask::status() const
{
    return std::string(m_status);
}

void AssetImportTask::importAsset()
{
    SCOPED_PROFILE_ZONE();

    if (!FileIO::fileReadable(m_assetFilePath)) {
        ARKOSE_LOG(Error, "Trying to import asset '{}' that is not readable / doesn't exist.", m_assetFilePath);
        m_error = true;
        return;
    }

    if (m_assetFilePath.extension() == ".gltf" || m_assetFilePath.extension() == ".glb") {
        importGltf();
        return;
    }

    ARKOSE_LOG(Error, "Trying to import asset '{}' of unsupported file type.", m_assetFilePath);
    m_error = true;
}

void AssetImportTask::importGltf()
{
    SCOPED_PROFILE_ZONE();

    // (Just to avoid too many changes part of this big refactor)
    ImportResult& result = m_result;
    AssetImporterOptions& options = m_options;

    auto updateStatus = [this](char const* status) {
        m_status = status;
        ARKOSE_LOG(Info, "{}", status);
    };

    updateStatus("Loading glTF file");

    GltfLoader gltfLoader {};
    m_result = gltfLoader.load(m_assetFilePath);

    // Figure out total number of work items
    m_totalItemCount = 1 + // initial glTF file load
        m_result.images.size() + // compress images
        m_result.images.size() + // writing images
        m_result.materials.size() + // writing materials
        m_result.meshes.size() + // resolving mesh materials
        m_result.meshes.size() + // process meshes
        m_result.meshes.size() + // write meshes
        m_result.skeletons.size() + // write skeletons
        m_result.animations.size() + // write animations
        1; // write set asset

    m_processedItemCount += 1;

    if (m_result.images.size() > 0) {
        updateStatus("Generating MIP maps & compressing textures");
    }

    // Compress all images (the slow part of this process) in parallel
    ParallelFor(result.images.size(), [&](size_t idx) {
        auto& image = result.images[idx];

        // Only compress here if we're not able to (or don't want to) defer it with an image spec
        if (image && !image->hasSourceAsset() || !options.generateImageSpecs) {

            if (options.generateMipmaps && image->numMips() == 1) {
                // NOTE: Can fail!
                image->generateMipmaps();
            }

            if (options.blockCompressImages && !image->hasCompressedFormat()) {
                TextureCompressor textureCompressor {};
                if (image->type() == ImageType::NormalMap) {
                    image = textureCompressor.compressBC5(*image);
                } else {
                    image = textureCompressor.compressBC7(*image);
                }
            }
        }

        m_processedItemCount += 1;
    });

    if (result.images.size() > 0) {
        updateStatus("Writing images");
    }

    int unnamedImageIdx = 0;
    for (auto& image : result.images) {

        if (image) {

            std::string fileName;
            if (image->hasSourceAsset()) {
                fileName = image->sourceAssetFilePath().stem().string();
            } else if (image->name.size() > 0) {
                // TODO: Perform proper name de-duplication (only increment when two identical ones would be saved)
                fileName = fmt::format("{}{:04}", image->name, unnamedImageIdx++);
            } else {
                fileName = fmt::format("image{:04}", unnamedImageIdx++);
            }

            std::filesystem::path targetFilePath = (m_targetDirectory / fileName).replace_extension(ImageAsset::AssetFileExtension);
            image->setAssetFilePath(targetFilePath);

            if (options.generateImageSpecs && image->hasSourceAsset()) {

                auto& imgSpec = result.imageSpecs.emplace_back(std::make_unique<ImageBakeSpec>());
                imgSpec->inputImage = image->sourceAssetFilePath().generic_string();
                imgSpec->targetImage = targetFilePath.generic_string();
                imgSpec->type = image->type();
                imgSpec->generateMipmaps = options.generateMipmaps;
                imgSpec->compress = options.blockCompressImages;

                std::string imgSpecExtension = ImageAsset::AssetFileExtension + std::string(".imgspec");
                std::filesystem::path imgSpecFilePath = (m_tempDirectory / fileName).replace_extension(imgSpecExtension);
                imgSpec->writeToFile(imgSpecFilePath);
                imgSpec->selfPath = imgSpecFilePath;

            } else {
                image->writeToFile(targetFilePath, AssetStorage::Binary);
                result.imageSpecs.emplace_back(); // emplace null-spec just to ensure arrays match
            }
        }

        m_processedItemCount += 1;
    }

    if (m_result.materials.size() > 0) {
        updateStatus("Writing materials");
    }

    std::unordered_map<std::string, int> materialNameMap {};
    for (auto& material : result.materials) {

        // Resolve references (paths) to image assets
        // (The glTF loader will use its local glTF indices while loading, since we don't yet know the file paths)

        auto resolveImageFilePath = [&](std::optional<MaterialInput>& materialInput) {
            if (materialInput.has_value()) {
                int gltfIdx = materialInput->userData;
                ARKOSE_ASSERT(gltfIdx >= 0 && gltfIdx < narrow_cast<int>(result.images.size()));
                auto& image = result.images[gltfIdx];
                if (image) {
                    materialInput->image = image->assetFilePath().generic_string();
                }
            }
        };

        resolveImageFilePath(material->baseColor);
        resolveImageFilePath(material->emissiveColor);
        resolveImageFilePath(material->normalMap);
        resolveImageFilePath(material->materialProperties);
        resolveImageFilePath(material->occlusionMap);

        std::string fileName = material->name;
        if (fileName.empty()) {
            fileName = "material";
        }

        int count = materialNameMap[fileName]++;
        if (count > 0 || fileName == "material") {
            fileName = fmt::format("{}{:04}", fileName, count);
        }

        std::filesystem::path targetFilePath = (m_targetDirectory / fileName).replace_extension(MaterialAsset::AssetFileExtension);

        material->writeToFile(targetFilePath, AssetStorage::Json);
        material->setAssetFilePath(targetFilePath);

        m_processedItemCount += 1;
    }

    if (m_result.meshes.size() > 0) {
        updateStatus("Resolving mesh materials");
    }

    for (auto& mesh : result.meshes) {

        // Resolve references (paths) to material assets
        // (The glTF loader will use its local glTF indices while loading, since we don't yet know the file paths)

        for (MeshLODAsset& lod : mesh->LODs) {
            for (MeshSegmentAsset& meshSegment : lod.meshSegments) {
                if (meshSegment.userData != -1) {
                    int gltfIdx = meshSegment.userData;
                    ARKOSE_ASSERT(gltfIdx >= 0 && gltfIdx < narrow_cast<int>(result.materials.size()));
                    auto& material = result.materials[gltfIdx];
                    meshSegment.material = material->assetFilePath().generic_string();
                }
            }
        }

        m_processedItemCount += 1;
    }

    if (m_result.meshes.size() > 0) {
        updateStatus("Processing meshes");
    }

    ParallelFor(result.meshes.size(), [&](size_t idx) {
        auto& mesh = result.meshes[idx];
        for (MeshLODAsset& lod : mesh->LODs) {
            for (MeshSegmentAsset& meshSegment : lod.meshSegments) {
                meshSegment.processForImport();
            }
        }

        m_processedItemCount += 1;
    });

    if (m_result.meshes.size() > 0) {
        updateStatus("Writing meshes");
    }

    std::unordered_map<std::string, int> meshNameMap {};
    for (auto& mesh : result.meshes) {

        std::string fileName = mesh->name;
        if (fileName.empty()) {
            fileName = "mesh";
        }

        int count = meshNameMap[fileName]++;
        if (count > 0 || fileName == "mesh") {
            fileName = fmt::format("{}{:04}", fileName, count);
        }

        std::filesystem::path targetFilePath = (m_targetDirectory / fileName).replace_extension(MeshAsset::AssetFileExtension);

        // TODO: Json is currently super slow with all the data we have, even for smaller meshes, but if we separate out the core data it will be fine.
        AssetStorage assetStorage = options.saveMeshesInTextualFormat ? AssetStorage::Json : AssetStorage::Binary;
        mesh->writeToFile(targetFilePath, assetStorage);
        mesh->setAssetFilePath(targetFilePath);

        m_processedItemCount += 1;
    }

    if (m_result.skeletons.size() > 0) {
        updateStatus("Writing skeletons");
    }

    std::unordered_map<std::string, int> skeletonNameMap {};
    for (auto& skeleton : result.skeletons) {

        std::string fileName = skeleton->name;
        if (fileName.empty()) {
            fileName = "skeleton";
        }

        int count = skeletonNameMap[fileName]++;
        if (count > 0 || fileName == "skeleton") {
            fileName = fmt::format("{}{:04}", fileName, count);
        }

        std::filesystem::path targetFilePath = (m_targetDirectory / fileName).replace_extension(SkeletonAsset::AssetFileExtension);

        skeleton->writeToFile(targetFilePath, AssetStorage::Json);
        skeleton->setAssetFilePath(targetFilePath);

        m_processedItemCount += 1;
    }

    if (m_result.animations.size() > 0) {
        updateStatus("Writing animations");
    }

    std::unordered_map<std::string, int> animationNameMap {};
    for (auto& animation : result.animations) {

        std::string fileName = animation->name;
        if (fileName.empty()) {
            fileName = "animation";
        }

        int count = animationNameMap[fileName]++;
        if (count > 0 || fileName == "animation") {
            fileName = fmt::format("{}{:04}", fileName, count);
        }

        std::filesystem::path targetFilePath = (m_targetDirectory / fileName).replace_extension(AnimationAsset::AssetFileExtension);

        animation->writeToFile(targetFilePath, AssetStorage::Json);
        animation->setAssetFilePath(targetFilePath);

        m_processedItemCount += 1;
    }

    // Make an SetAsset for the imported asset
    {
        result.set = std::make_unique<SetAsset>();

        std::string fileName = m_assetFilePath.filename().replace_extension("").string();
        result.set->name = fileName;

        for (MeshInstance const& meshInstance : result.meshInstances) {
            NodeAsset* nodeAsset = result.set->rootNode.createChildNode();
            nodeAsset->name = meshInstance.mesh->name;
            nodeAsset->transform = meshInstance.transform;
            nodeAsset->meshIndex = narrow_cast<i32>(result.set->meshAssets.size());
            result.set->meshAssets.emplace_back(meshInstance.mesh->assetFilePath().generic_string());
        }

        std::filesystem::path targetFilePath = (m_targetDirectory / fileName).replace_extension(SetAsset::AssetFileExtension);

        result.set->writeToFile(targetFilePath, AssetStorage::Json);
        result.set->setAssetFilePath(targetFilePath);

        m_processedItemCount += 1;
    }

    ARKOSE_ASSERT(progress() == 1.0f);
    updateStatus("Done");
}
