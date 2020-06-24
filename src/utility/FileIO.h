#pragma once

#include <optional>
#include <string>
#include <vector>

namespace FileIO {

using BinaryData = std::vector<char>;
std::optional<BinaryData> readEntireFileAsByteBuffer(const std::string& filePath);

std::optional<std::string> readEntireFile(const std::string& filePath);

bool isFileReadable(const std::string& filePath);

}
