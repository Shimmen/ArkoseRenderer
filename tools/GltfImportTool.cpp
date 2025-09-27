#include <asset/import/AssetImporter.h>
#include <core/Logging.h>
#include <core/parallel/TaskGraph.h>

int main(int argc, char* argv[])
{
    if (argc < 3) {
        // TODO: Add support for named command line arguments!
        ARKOSE_LOG(Error, "GltfImportTool: must be called as\n> GltfImportTool <SourceGltfFile> <TargetDirectory>");
        return 1;
    }

    std::filesystem::path inputAsset = argv[1];
    ARKOSE_LOG(Info, "GltfImportTool: importing asset '{}'", inputAsset);

    std::filesystem::path targetDirectory = argv[2];
    ARKOSE_LOG(Info, "GltfImportTool: will write results to '{}'", targetDirectory);

    AssetImporterOptions options { .alwaysMakeImageAsset = true,
                                   .generateMipmaps = true,
                                   .blockCompressImages = true };

    ImportResult result;

    // Import asset
    {
        std::unique_ptr<AssetImportTask> importTask = AssetImportTask::create(inputAsset, targetDirectory, options);
        importTask->executeSynchronous();

        result = std::move(*importTask->result());
    }

    // Create dependency file
    {
        std::string originalExt = inputAsset.extension().string();
        std::filesystem::path dependencyFile = targetDirectory / inputAsset.filename().replace_extension(originalExt + ".dep");
        ARKOSE_LOG(Info, "GltfImportTool: writing dependency file '{}'", dependencyFile);

        std::string dependencyData = "";

        // Not needed / breaks the build in AssetCooker if this is included. Not sure why..
        //dependencyData += fmt::format("INPUT: {}\n", inputAsset);

        auto addOutputDependency = [&dependencyData]<typename AssetType>(AssetType const& asset) {
            // TODO: Ensure all meshes/images/etc. are relative to `targetDirectory`!
            // TODO: Write paths relative to the 'imported' directory!
            std::string dependencyPath = asset.assetFilePath().generic_string();
            dependencyData += fmt::format("OUTPUT: {}\n", dependencyPath);
        };

        for (auto const& image : result.images) {
            addOutputDependency(*image);
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

        FileIO::writeTextDataToFile(dependencyFile, dependencyData);
    }

    return 0;
}
