#include <asset/ImageAsset.h>
#include <core/Logging.h>
#include <utility/FileIO.h>

int main(int argc, char* argv[])
{
    if (argc < 3) {
        // TODO: Add support for named command line arguments!
        ARKOSE_LOG(Error, "ImgAssetBakeTool: must be called as\n> ImgAssetBakeTool <SourceImageFile> <TargetImageFile>");
        return 1;
    }

    std::filesystem::path inputFile = argv[1];
    ARKOSE_LOG(Info, "ImgAssetBakeTool: baking image file '{}'", inputFile);

    std::filesystem::path outputFile = argv[2];
    ARKOSE_LOG(Info, "ImgAssetBakeTool: will write baked file to '{}'", outputFile);

    // NOTE: Can take both .dds files & other various image formats (png, jpg, etc.)
    ImageAsset* imageAsset = ImageAsset::loadOrCreate(inputFile);

    // TODO: Optionally also generate mipmaps, block compress, etc.

    imageAsset->writeToFile(outputFile, AssetStorage::Binary);

    return 0;
}
