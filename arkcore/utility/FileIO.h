#pragma once

#include "core/Types.h"
#include "utility/Profiling.h"
#include <optional>
#include <filesystem>
#include <functional>
#include <fstream>
#include <string>
#include <vector>

namespace FileIO {

bool fileReadable(std::filesystem::path const& filePath);

void ensureDirectory(std::filesystem::path const& directoryPath);
void ensureDirectoryForFile(std::filesystem::path const& filePath);

void writeTextDataToFile(std::filesystem::path const& filePath, std::string_view text);
void writeBinaryDataToFile(std::filesystem::path const& filePath, std::byte const* data, size_t size);

std::optional<std::string> readFile(std::filesystem::path const& filePath);
bool readFileLineByLine(std::filesystem::path const& filePath, std::function<LoopAction(const std::string& line)>);

template<typename T>
void writeBinaryDataToFile(std::filesystem::path const& filePath, const std::vector<T>& vector)
{
    std::byte const* data = reinterpret_cast<std::byte const*>(vector.data());
    size_t size = sizeof(T) * vector.size();

    writeBinaryDataToFile(filePath, data, size);
}

template<typename T>
std::optional<std::vector<T>> readBinaryDataFromFile(std::filesystem::path const& filePath)
{
    SCOPED_PROFILE_ZONE();

    std::ifstream file(filePath, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        return {};
    }

    size_t sizeInBytes = file.tellg();
    size_t sizeInTs = sizeInBytes / sizeof(T);
    std::vector<T> binaryData(sizeInTs);

    file.seekg(0);
    file.read(reinterpret_cast<char*>(binaryData.data()), sizeInBytes);

    file.close();
    return binaryData;
}

}
