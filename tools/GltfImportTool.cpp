#include <asset/import/AssetImporter.h>
#include <core/Logging.h>
#include <core/parallel/TaskGraph.h>

int main(int argc, char* argv[])
{
    if (argc < 3) {
        // TODO: Add support for named command line arguments!
        ARKOSE_LOG(Error, "GltfImportTool: must be called as\n> GltfImportTool <SourceGltfFile> <TargetDirectory> <TempDirectory>");
        return 1;
    }

    std::filesystem::path inputAsset = argv[1];
    ARKOSE_LOG(Info, "GltfImportTool: importing asset '{}'", inputAsset);

    std::filesystem::path targetDirectory = argv[2];
    ARKOSE_LOG(Info, "GltfImportTool: will write results to '{}'", targetDirectory);

    std::filesystem::path tempDirectory = argv[3];
    ARKOSE_LOG(Info, "GltfImportTool: will write temp files to '{}'", tempDirectory);

    AssetImporterOptions options { .generateMipmaps = true,
                                   .blockCompressImages = true,
                                   .generateImageSpecs = true };

    ImportResult result;

    // Import asset
    {
        std::unique_ptr<AssetImportTask> importTask = AssetImportTask::create(inputAsset, targetDirectory, tempDirectory, options);
        importTask->executeSynchronous();

        result = std::move(*importTask->result());
    }

    // Create dependency file
    {
        std::string originalExt = inputAsset.extension().string();
        std::filesystem::path dependencyFilePath = tempDirectory / inputAsset.filename().replace_extension(originalExt + ".dep");
        ARKOSE_LOG(Info, "GltfImportTool: writing dependency file '{}'", dependencyFilePath);

        std::string dependencyData = "";

        // Not needed / breaks the build in AssetCooker if this is included. Not sure why..
        //dependencyData += fmt::format("INPUT: {}\n", inputAsset);

        auto addOutputDependency = [&dependencyData]<typename AssetType>(AssetType const& asset) {
            // TODO: Ensure all meshes/images/etc. are relative to `targetDirectory`!
            // TODO: Write paths relative to the 'imported' directory!
            std::string dependencyPath = asset.assetFilePath().generic_string();
            dependencyData += fmt::format("OUTPUT: {}\n", dependencyPath);
        };

        auto addImageSpecOutputDependency = [&](ImageBakeSpec const& imageSpec) {
            dependencyData += fmt::format("OUTPUT: {}\n", imageSpec.selfPath.generic_string());
        };

        ARKOSE_ASSERT(result.images.size() == result.imageSpecs.size());
        for (size_t imgIdx = 0; imgIdx < result.images.size(); ++imgIdx) {
            auto& imageSpec = result.imageSpecs[imgIdx];
            auto& imageAsset = result.images[imgIdx];

            if (imageSpec != nullptr) {
                addImageSpecOutputDependency(*imageSpec);
            } else {
                addOutputDependency(*imageAsset);
            }
        }

        for (auto const& material : result.materials) {
            addOutputDependency(*material);
        }

        for (auto const& mesh : result.meshes) {
            addOutputDependency(*mesh);
        }

        for (auto const& skeleton : result.skeletons) {
            addOutputDependency(*skeleton);
        }

        for (auto const& animation : result.animations) {
            addOutputDependency(*animation);
        }

        if (result.set) {
            addOutputDependency(*result.set);
        }

        FileIO::writeTextDataToFile(dependencyFilePath, dependencyData);
    }

    return 0;
}
