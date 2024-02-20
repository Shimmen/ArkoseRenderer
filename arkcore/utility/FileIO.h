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

size_t indexOfLashSlash(std::string_view path);
std::string_view extractDirectoryFromPath(std::string_view path);
std::string_view extractFileNameFromPath(std::string_view path);
std::string_view removeExtensionFromPath(std::string_view path);

std::string normalizePath(std::string_view absolutePath);

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

uint8_t* readBinaryDataFromFileRawPtr(const std::string& filePath, size_t* outSize);

void writeTextDataToFile(const std::string& filePath, const std::string& text);
void writeBinaryDataToFile(const std::string& filePath, const char* data, size_t size);

inline void writeBinaryDataToFile(const std::string& filePath, const uint8_t* data, size_t size)
{
    writeBinaryDataToFile(filePath, reinterpret_cast<const char*>(data), size);
}

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

}
