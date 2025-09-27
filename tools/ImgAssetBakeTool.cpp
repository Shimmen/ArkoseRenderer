#include <asset/ImageAsset.h>
#include <asset/TextureCompressor.h>
#include <asset/misc/ImageBakeSpec.h>
#include <core/Logging.h>
#include <utility/FileIO.h>

int main(int argc, char* argv[])
{
    if (argc < 2) {
        // TODO: Add support for named command line arguments!
        ARKOSE_LOG(Error, "ImgAssetBakeTool: not enough arguments!");
        return 1;
    }

    std::filesystem::path inputFile = argv[1];
    if (inputFile.has_extension() && inputFile.extension() == ".imgspec") {

        ARKOSE_LOG(Info, "ImgAssetBakeTool: parsing image bake spec");

        ImageBakeSpec imgSpec;
        if (!imgSpec.readFromFile(inputFile)) {
            ARKOSE_LOG(Error, "ImgAssetBakeTool: failed to parse image bake spec");
            return 1;
        }

        ARKOSE_LOG(Info, "ImgAssetBakeTool: loading image image '{}'...", imgSpec.inputImage);

        std::unique_ptr<ImageAsset> imageAsset = ImageAsset::createFromSourceAsset(imgSpec.inputImage);

        if (!imageAsset) {
            ARKOSE_LOG(Error, "ImgAssetBakeTool: failed to load image");
            return 1;
        }
        switch (imgSpec.type) {
        case ImageType::sRGBColor:
            imageAsset->setType(ImageType::sRGBColor);
            break;
        case ImageType::GenericData:
            imageAsset->setType(ImageType::GenericData);
            break;
        case ImageType::NormalMap:
            imageAsset->setType(ImageType::NormalMap);
            break;
        default:
            imageAsset->setType(ImageType::Unknown);
            break;
        }

        if (imageAsset->numMips() == 1) {
            ARKOSE_LOG(Info, "ImgAssetBakeTool: generating mipmaps...");
            if (!imageAsset->generateMipmaps()) {
                ARKOSE_LOG(Warning, "ImgAssetBakeTool: failed to generate mipmaps");
            }
        } else {
            ARKOSE_LOG(Info, "ImgAssetBakeTool: image already has mipmaps, skipping generation");
        }

        bool isAlreadyCompressed = imageFormatIsBlockCompressed(imageAsset->format());
        if (isAlreadyCompressed) {
            ARKOSE_LOG(Info, "ImgAssetBakeTool: image is already block compressed, skipping compression");
        } else if (imgSpec.compress) {

            ARKOSE_LOG(Info, "ImgAssetBakeTool: compressing image...");

            TextureCompressor textureCompressor;
            switch (imgSpec.type) {
            case ImageType::sRGBColor:
            case ImageType::GenericData:
                imageAsset = textureCompressor.compressBC7(*imageAsset);
                break;
            case ImageType::NormalMap:
                imageAsset = textureCompressor.compressBC5(*imageAsset);
                break;
            case ImageType::Unknown:
                ARKOSE_LOG(Warning, "ImgAssetBakeTool: compressing image '{}' of unknown type as BC7 (ideally we have a type!)", imgSpec.inputImage);
                imageAsset = textureCompressor.compressBC7(*imageAsset);
                break;
            default:
                ARKOSE_LOG(Error, "ImgAssetBakeTool: failed to compress image of type '{}'", imgSpec.type);
                return 1;
            }
        }

        ARKOSE_LOG(Info, "ImgAssetBakeTool: writing image...");

        bool writeSuccess = imageAsset->writeToFile(imgSpec.targetImage, AssetStorage::Binary);
        if (writeSuccess) {
            ARKOSE_LOG(Info, "ImgAssetBakeTool: wrote baked image to '{}'", imgSpec.targetImage);
        } else {
            ARKOSE_LOG(Info, "ImgAssetBakeTool: failed to write baked image to '{}'", imgSpec.targetImage);
            return 1;
        }

        ARKOSE_LOG(Info, "ImgAssetBakeTool: writing dependency file...");

        std::string dependencyData = fmt::format("INPUT: {}\nOUTPUT: {}\n", imgSpec.inputImage, imgSpec.targetImage);

        std::filesystem::path dependencyFilePath = inputFile.replace_extension(inputFile.extension().string() + ".dep");
        FileIO::writeTextDataToFile(dependencyFilePath, dependencyData);
        ARKOSE_LOG(Info, "ImgAssetBakeTool: wrote dependency file to '{}'", dependencyFilePath.generic_string());

    } else {

        if (argc < 3) {
            // TODO: Add support for named command line arguments!
            ARKOSE_LOG(Error, "ImgAssetBakeTool: if no spec file, must be called as\n> ImgAssetBakeTool <SourceImageFile> <TargetImageFile>");
            return 1;
        }

        ARKOSE_LOG(Info, "ImgAssetBakeTool: baking image file '{}'", inputFile);

        std::filesystem::path outputFile = argv[2];
        ARKOSE_LOG(Info, "ImgAssetBakeTool: will write baked file to '{}'", outputFile);

        // NOTE: Can take both .dds files & other various image formats (png, jpg, etc.)
        std::unique_ptr<ImageAsset> imageAsset = ImageAsset::createFromSourceAsset(inputFile);
        imageAsset->writeToFile(outputFile, AssetStorage::Binary);
    }

    return 0;
}
