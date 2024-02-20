#include "ParseContext.h"

#include "core/Logging.h"

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
    return -1;
}

float ParseContext::nextAsFloat(const char* token)
{
    auto maybeFloat = nextAsFloat();
    if (maybeFloat)
        return maybeFloat.value();
    ARKOSE_LOG(Fatal, "Error parsing <{}> in {} file '{}'", token, m_fileType, m_path);
    return -1.0f;
}
