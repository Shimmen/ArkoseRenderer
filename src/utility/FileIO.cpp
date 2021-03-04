#include "FileIO.h"

#include "utility/Logging.h"
#include "utility/Profiling.h"
#include <fstream>

#ifdef WIN32
#include <sys/stat.h>
#else
#endif

std::optional<FileIO::BinaryData> FileIO::readEntireFileAsByteBuffer(const std::string& filePath)
{
    SCOPED_PROFILE_ZONE();

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
    SCOPED_PROFILE_ZONE();

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

bool FileIO::readFileLineByLine(const std::string& filePath, std::function<NextAction(const std::string&)> lineCallback)
{
    std::ifstream file(filePath);

    if (!file.is_open())
        return false;

    std::string line;
    while (std::getline(file, line)) {
        NextAction nextAction = lineCallback(line);
        if (nextAction == NextAction::Stop) {
            break;
        }
    }

    return true;
}

bool FileIO::isFileReadable(const std::string& filePath)
{
    SCOPED_PROFILE_ZONE();

#ifdef WIN32
    struct _stat statObject;
    int result = _stat(filePath.c_str(), &statObject);
    return result == 0;
#else
    // TODO: Use stat for other platforms too. Should work the same, but can't test that now..
    std::ifstream file(filePath);
    bool isGood = file.good();
    return isGood;
#endif

    
}


FileIO::ParseContext::ParseContext(const std::string& fileType, const std::string& filePath)
    : m_fileType(fileType)
    , m_path(filePath)
    , m_stream(filePath)
{
}

bool FileIO::ParseContext::isValid() const
{
    return m_stream.good();
}

std::string FileIO::ParseContext::nextLine()
{
    std::string line;
    if (std::getline(m_stream, line))
        return line;
    return {};
}

std::optional<int> FileIO::ParseContext::nextAsInt()
{
    int intValue;
    if (m_stream >> intValue)
        return intValue;
    return {};
}

std::optional<float> FileIO::ParseContext::nextAsFloat()
{
    float floatValue;
    if (m_stream >> floatValue)
        return floatValue;
    return {};
}

int FileIO::ParseContext::nextAsInt(const char* token)
{
    auto maybeInt = nextAsInt();
    if (maybeInt)
        return maybeInt.value();
    LogErrorAndExit("Error parsing <%s> in %s file \"%s\"\n", token, m_fileType.c_str(), m_path.c_str());
    return -1;
}

float FileIO::ParseContext::nextAsFloat(const char* token)
{
    auto maybeFloat = nextAsFloat();
    if (maybeFloat)
        return maybeFloat.value();
    LogErrorAndExit("Error parsing <%s> in %s file \"%s\"\n", token, m_fileType.c_str(), m_path.c_str());
    return -1.0f;
}
