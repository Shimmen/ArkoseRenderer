#include <core/Logging.h>
#include <utility/FileIO.h>

int main(int argc, char* argv[])
{
    if (argc < 3) {
        ARKOSE_LOG(Error, "CopyFileTool: must be called as\n> CopyFileTool <SourceFile> <TargetFile>");
        return 1;
    }

    std::filesystem::path inputFile = argv[1];
    std::filesystem::path outputFile = argv[2];
    ARKOSE_LOG(Info, "CopyFileTool: copying '{}' to '{}'", inputFile, outputFile);

    std::optional<std::vector<std::byte>> maybeData = FileIO::readBinaryDataFromFile<std::byte>(inputFile);

    if (!maybeData) {
        ARKOSE_LOG(Error, "Failed to read file '{}'", inputFile);
        return 1;
    }

    std::vector<std::byte> const& data = *maybeData;
    FileIO::writeBinaryDataToFile(outputFile, data);

    return 0;
}
