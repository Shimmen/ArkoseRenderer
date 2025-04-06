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

    // TODO: Add an option to run the AssetImportTask in a single thread! Makes more sense in a tool setting like this.
    TaskGraph::initialize();

    std::string inputAsset = argv[1];
    ARKOSE_LOG(Info, "GltfImportTool: importing asset '{}'", inputAsset);

    std::string targetDirectory = argv[2];
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
        std::string dependencyFile = targetDirectory + "dependencies.dep";
        ARKOSE_LOG(Info, "GltfImportTool: writing dependency file '{}'", dependencyFile);

        std::string dependencyData = "";

        dependencyData += fmt::format("INPUT: {}\n", inputAsset);

        for (auto const& image : result.images) {
            dependencyData += fmt::format("OUTPUT: {}\n", image->assetFilePath());
        }

        for (auto const& material : result.materials) {
            dependencyData += fmt::format("OUTPUT: {}\n", material->assetFilePath());
        }

        for (auto const& mesh : result.meshes) {
            dependencyData += fmt::format("OUTPUT: {}\n", mesh->assetFilePath());
        }

        for (auto const& skeleton : result.skeletons) {
            dependencyData += fmt::format("OUTPUT: {}\n", skeleton->assetFilePath());
        }

        for (auto const& animation : result.animations) {
            dependencyData += fmt::format("OUTPUT: {}\n", animation->assetFilePath());
        }

        FileIO::writeTextDataToFile(dependencyFile, dependencyData);
    }

    TaskGraph::shutdown();
    return 0;
}
