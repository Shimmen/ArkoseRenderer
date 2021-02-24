#pragma once

#include <optional>
#include <functional>
#include <fstream>
#include <string>
#include <vector>

namespace FileIO {

using BinaryData = std::vector<char>;
std::optional<BinaryData> readEntireFileAsByteBuffer(const std::string& filePath);

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
