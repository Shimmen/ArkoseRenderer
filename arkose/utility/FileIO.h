#pragma once

#include "utility/Profiling.h"
#include <optional>
#include <functional>
#include <fstream>
#include <string>
#include <vector>

namespace FileIO {

void ensureDirectory(const std::string& directoryPath);
void ensureDirectoryForFile(const std::string& filePath);

template<typename T>
std::optional<std::vector<T>> readBinaryDataFromFile(const std::string& filePath)
{
    SCOPED_PROFILE_ZONE();

    // Open file as binary and immediately seek to the end
    std::ifstream file(filePath, std::ios::ate | std::ios::binary);
    if (!file.is_open())
        return {};

    size_t sizeInBytes = file.tellg();
    size_t sizeInTs = sizeInBytes / sizeof(T);
    std::vector<T> binaryData(sizeInTs);

    file.seekg(0);
    file.read((char*)binaryData.data(), sizeInBytes);

    file.close();
    return binaryData;
}

void writeTextDataToFile(const std::string& filePath, const std::string& text);
void writeBinaryDataToFile(const std::string& filePath, const char* data, size_t size);

template<typename T>
void writeBinaryDataToFile(const std::string& filePath, const std::vector<T>& vector)
{
    const char* data = (const char*)vector.data();
    size_t size = sizeof(T) * vector.size();
    writeBinaryDataToFile(filePath, data, size);
}

std::optional<std::string> readEntireFile(const std::string& filePath);

enum class NextAction {
    Continue,
    Stop,
};
bool readFileLineByLine(const std::string& filePath, std::function<NextAction(const std::string& line)>);

bool isFileReadable(const std::string& filePath);

class ParseContext {
public:
    ParseContext(const std::string& fileType, const std::string& filePath);

    bool isValid() const;

    std::string nextLine();

    std::optional<int> nextAsInt();
    std::optional<float> nextAsFloat();

    int nextAsInt(const char* token);
    float nextAsFloat(const char* token);

private:
    std::string m_fileType;
    std::string m_path;
    std::ifstream m_stream;

};

}
