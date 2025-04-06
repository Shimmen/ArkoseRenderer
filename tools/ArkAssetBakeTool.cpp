#include <asset/AnimationAsset.h>
#include <asset/ImageAsset.h>
#include <asset/LevelAsset.h>
#include <asset/MaterialAsset.h>
#include <asset/MeshAsset.h>
#include <asset/SkeletonAsset.h>
#include <core/Logging.h>
#include <utility/FileIO.h>

int main(int argc, char* argv[])
{
    if (argc < 3) {
        // TODO: Add support for named command line arguments!
        ARKOSE_LOG(Error, "ArkAssetBakeTool: must be called as\n> ArkAssetBakeTool <SourceArkFile> <TargetArkFile>");
        return 1;
    }

    std::filesystem::path inputFile = argv[1];
    ARKOSE_LOG(Info, "ArkAssetBakeTool: baking arkose asset file '{}'", inputFile);

    std::filesystem::path outputFile = argv[2];
    ARKOSE_LOG(Info, "ArkAssetBakeTool: will write baked file to '{}'", outputFile);

    if (!inputFile.has_extension()) {
        ARKOSE_LOG(Error, "ArkAssetBakeTool: input file has no extension so we can't derive the asset type");
        return 1;
    }

    std::filesystem::path extension = inputFile.extension();

    if (extension == AnimationAsset::AssetFileExtension) {

        ARKOSE_LOG(Info, "ArkAssetBakeTool: loading animation asset file '{}'", inputFile);
        AnimationAsset* animationAsset = AnimationAsset::load(inputFile);
        animationAsset->writeToFile(outputFile, AssetStorage::Binary);

    } else if (extension == ImageAsset::AssetFileExtension) {

        ARKOSE_LOG(Info, "ArkAssetBakeTool: loading image asset file '{}'", inputFile);
        ImageAsset* imageAsset = ImageAsset::loadOrCreate(inputFile);
        imageAsset->writeToFile(outputFile, AssetStorage::Binary);

    } else if (extension == LevelAsset::AssetFileExtension) {

        ARKOSE_LOG(Info, "ArkAssetBakeTool: loading level asset file '{}'", inputFile);
        LevelAsset* levelAsset = LevelAsset::load(inputFile);
        levelAsset->writeToFile(outputFile, AssetStorage::Binary);

    } else if (extension == MaterialAsset::AssetFileExtension) {

        ARKOSE_LOG(Info, "ArkAssetBakeTool: loading material asset file '{}'", inputFile);
        MaterialAsset* materialAsset = MaterialAsset::load(inputFile);
        materialAsset->writeToFile(outputFile, AssetStorage::Binary);

    } else if (extension == MeshAsset::AssetFileExtension) {

        ARKOSE_LOG(Info, "ArkAssetBakeTool: loading mesh asset file '{}'", inputFile);
        MeshAsset* meshAsset = MeshAsset::load(inputFile);
        meshAsset->writeToFile(outputFile, AssetStorage::Binary);

    } else if (extension == SkeletonAsset::AssetFileExtension) {

        ARKOSE_LOG(Info, "ArkAssetBakeTool: loading skeleton asset file '{}'", inputFile);
        SkeletonAsset* skeletonAsset = SkeletonAsset::load(inputFile);
        skeletonAsset->writeToFile(outputFile, AssetStorage::Binary);

    } else {
        ARKOSE_LOG(Error, "ArkAssetBakeTool: unknown arkose asset type '{}'", extension);
        return 1;
    }

    return 0;
}
