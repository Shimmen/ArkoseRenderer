#pragma once

#include <optional>
#include <string>
#include <fstream>

class ParseContext {
public:
    ParseContext(std::string const& fileType, std::string const& filePath);

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
