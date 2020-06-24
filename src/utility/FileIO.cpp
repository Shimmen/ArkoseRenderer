#include "FileIO.h"

#include <fstream>

std::optional<FileIO::BinaryData> FileIO::readEntireFileAsByteBuffer(const std::string& filePath)
{
    // Open file as binary and immediately seek to the end
    std::ifstream file(filePath, std::ios::ate | std::ios::binary);

    if (!file.is_open())
        return {};

    size_t sizeInBytes = file.tellg();
    FileIO::BinaryData binaryData(sizeInBytes);

    file.seekg(0);
    file.read(binaryData.data(), sizeInBytes);

    file.close();
    return binaryData;
}

std::optional<std::string> FileIO::readEntireFile(const std::string& filePath)
{
    // Open file as binary and immediately seek to the end
    std::ifstream file(filePath, std::ios::ate | std::ios::binary);

    if (!file.is_open())
        return {};

    std::string contents {};

    size_t sizeInBytes = file.tellg();
    contents.reserve(sizeInBytes);
    file.seekg(0, std::ios::beg);

    contents.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());

    file.close();
    return contents;
}

bool FileIO::isFileReadable(const std::string& filePath)
{
    std::ifstream file(filePath);
    bool isGood = file.good();
    return isGood;
}
