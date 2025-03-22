#include "ParseContext.h"

#include "core/Logging.h"
#include <cctype>

ParseContext::ParseContext(std::string const& fileType, std::string const& filePath)
    : m_fileType(fileType)
    , m_path(filePath)
    , m_stream(filePath)
{
}

bool ParseContext::isValid() const
{
    return m_stream.good();
}

bool ParseContext::isEndOfFile() const
{
    int result = const_cast<ParseContext*>(this)->m_stream.peek();
    return result == std::char_traits<char>::eof();
}

char ParseContext::peekNextCharacter() const
{
    int result = const_cast<ParseContext*>(this)->m_stream.peek();
    if (result == std::char_traits<char>::eof()) {
        return '\0';
    }

    return static_cast<char>(result);
}

char ParseContext::consumeCharacter()
{
    char tempBuffer[2];
    m_stream.get(tempBuffer, sizeof(tempBuffer));
    return tempBuffer[0];
}

void ParseContext::consumeWhitespace(int count)
{
    while (count != 0) {
        if (std::isspace(peekNextCharacter())) {
            m_stream.ignore();
            count -= 1;
        } else {
            break;
        }
    }
}

void ParseContext::consumeNewline(int count, char newlineChar)
{
    while (count != 0) {
        if (peekNextCharacter() == newlineChar) {
            m_stream.ignore();
            count -= 1;
        } else {
            break;
        }
    }
}

void ParseContext::consumeDelimiter(char delimiter, bool alsoConsumeWhitespace)
{
    if (alsoConsumeWhitespace) { 
        consumeWhitespace();
    }

    if (peekNextCharacter() == delimiter) {
        consumeCharacter();
    }

    if (alsoConsumeWhitespace) {
        consumeWhitespace();
    }
}

std::optional<std::string> ParseContext::consumeStandardSymbol()
{
    std::string symbol = "";

    auto isAlphaOrUnderscore = [](char c) {
        return (c >= 'a' && c <= 'z')
            || (c >= 'A' && c <= 'Z')
            || (c == '_');
    };

    auto isAlphanumericOrUnderscore = [&](char c) {
        return isAlphaOrUnderscore(c)
            || (c >= '0' && c <= '9');
    };

    char c = peekNextCharacter();
    if (isAlphaOrUnderscore(c)) {
        symbol.push_back(consumeCharacter());
    } else {
        return std::nullopt;
    }

    while (true) {
        c = peekNextCharacter();
        if (isAlphanumericOrUnderscore(c)) {
            symbol.push_back(consumeCharacter());
        } else {
            break;
        }
    }

    return symbol;
}

std::optional<std::string> ParseContext::consumeString(char stringDelimiter)
{
    if (peekNextCharacter() == stringDelimiter) {
        consumeCharacter();
    } else {
        return std::nullopt;
    }

    std::string stringValue = "";
    while (true) {
        char c = peekNextCharacter();
        if (c == '\n') {
            // TODO: Handle newline during string parsing as an error
            // (but, for now, let's just end the string here on newline)
            break;
        } else if (c == stringDelimiter) {
            consumeCharacter();
            break;
        } else {
            stringValue.push_back(c);
            consumeCharacter();
        }
    }

    return stringValue;
}

std::string ParseContext::nextLine()
{
    std::string line;
    if (std::getline(m_stream, line))
        return line;
    return {};
}

std::optional<int> ParseContext::nextAsInt()
{
    int intValue;
    if (m_stream >> intValue)
        return intValue;
    return {};
}

std::optional<float> ParseContext::nextAsFloat()
{
    float floatValue;
    if (m_stream >> floatValue)
        return floatValue;
    return {};
}

int ParseContext::nextAsInt(const char* token)
{
    auto maybeInt = nextAsInt();
    if (maybeInt)
        return maybeInt.value();
    ARKOSE_LOG(Fatal, "Error parsing <{}> in {} file '{}'", token, m_fileType, m_path);
}

float ParseContext::nextAsFloat(const char* token)
{
    auto maybeFloat = nextAsFloat();
    if (maybeFloat)
        return maybeFloat.value();
    ARKOSE_LOG(Fatal, "Error parsing <{}> in {} file '{}'", token, m_fileType, m_path);
}
