#include <asset/ImageAsset.h>
#include <asset/external/DDSImage.h>
#include <asset/external/IESProfile.h>
#include <core/Logging.h>
#include <utility/FileIO.h>
#include <utility/ToolUtilities.h>

int main(int argc, char* argv[])
{
    if (argc < 3) {
        // TODO: Add support for named command line arguments!
        ARKOSE_LOG(Error, "IESConvertTool: must be called as\n> IESConvertTool <SourceIESFile> <TargetDDSFile>");
        return 1;
    }

    std::filesystem::path inputFile = argv[1];
    ARKOSE_LOG(Info, "IESConvertTool: converting IES file '{}'", inputFile);

    std::filesystem::path outputFile = argv[2];
    ARKOSE_LOG(Info, "IESConvertTool: will write DDS file to '{}'", outputFile);

    // Load the IES profile

    IESProfile profile;
    profile.load(inputFile);

    // Generate the image data

    constexpr u32 size = 256;
    std::vector<float> pixels = profile.assembleLookupTextureData<float>(size); // TODO: Use half floats!

    // Write the image to disk

    uint8_t* pixelData = reinterpret_cast<uint8_t*>(pixels.data());
    size_t pixelDataSize = pixels.size() * sizeof(float);

    bool writeSuccess = DDS::writeToFile(outputFile, pixelData, pixelDataSize, { size, size, 1 }, ImageFormat::R32F, false, 1);

    if (!writeSuccess) {
        ARKOSE_LOG(Error, "IESConvertTool: failed to write out DDS file");
        return 1;
    }

    return toolReturnCode();
}
