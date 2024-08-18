#pragma once

#include <optional>
#include <string>
#include <fstream>

class ParseContext {
public:
    ParseContext(std::string const& fileType, std::string const& filePath);

    bool isValid() const;

    bool isEndOfFile() const;

    char peekNextCharacter() const;
    char consumeCharacter();

    void consumeWhitespace(int count = -1);
    void consumeNewline(int count, char newlineChar = '\n');

    // a "standard" symbol in the regex format /[_a-zA-Z][_a-zA-Z0-9]*/
    std::optional<std::string> consumeStandardSymbol();

    // a string begining and ending with the string delimiter and containing any character except the delimiter or newline
    std::optional<std::string> consumeString(char stringDelimiter);

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
