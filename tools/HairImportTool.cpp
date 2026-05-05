#include <asset/external/HairFile.h>
#include <core/Logging.h>
#include <utility/ToolUtilities.h>

int main(int argc, char* argv[])
{
    if (argc < 2) {
        // TODO: Add support for named command line arguments!
        ARKOSE_LOG(Error, "HairImportTool: must be called as\n> HairImportTool <SourceHairFile> <TargetHairAssetFile>");
        return 1;
    }

    std::filesystem::path inputHairFile = argv[1];
    ARKOSE_LOG(Info, "HairImportTool: importing .hair file '{}'", inputHairFile);

    std::filesystem::path targetHairAssetFile = argv[2];
    ARKOSE_LOG(Info, "HairImportTool: will write hair asset to '{}'", targetHairAssetFile);

    // Import .hair file

    std::unique_ptr<HairFile> hairFile = HairFile::load(inputHairFile);
    if (hairFile == nullptr) {
        ARKOSE_LOG(Error, "HairImportTool: failed to load .hair file '{}'", inputHairFile);
        return 1;
    }

    // Convert & output hair asset

    std::unique_ptr<HairAsset> hairAsset = hairFile->createHairAsset();
    if (hairAsset == nullptr) {
        ARKOSE_LOG(Error, "HairImportTool: failed to convert .hair file to hair asset");
        return 1;
    }

    if (!hairAsset->writeToFile(targetHairAssetFile, AssetStorage::Binary)) {
        ARKOSE_LOG(Error, "HairImportTool: failed to write hair asset to file '{}'", targetHairAssetFile);
        return 1;
    }

    return toolReturnCode();
}
