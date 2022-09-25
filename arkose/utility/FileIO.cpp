#include "FileIO.h"

#include "core/Logging.h"
#include "utility/Profiling.h"
#include <fstream>

#ifdef WIN32
#include <sys/stat.h>
#include <Windows.h>
#else
#endif

void FileIO::ensureDirectory(const std::string& directoryPath)
{
    SCOPED_PROFILE_ZONE();

#if defined(WIN32)
    // Well, this is fucking stupid..
    size_t index = directoryPath.find_first_of('/');
    size_t offset = 0;
    while (index != std::string::npos) {
        std::string directoryBasePath = directoryPath.substr(0, index);
        CreateDirectory(directoryBasePath.c_str(), NULL);

        offset = index + 1;
        index = directoryPath.find_first_of('/', offset);
    }
    CreateDirectory(directoryPath.c_str(), NULL);
#else
#error Create a directory if it doesn't exist
#endif
}

void FileIO::ensureDirectoryForFile(const std::string& filePath)
{
    std::string directoryPath = filePath.substr(0, filePath.find_last_of('/'));
    ensureDirectory(directoryPath);
}

size_t FileIO::indexOfLashSlash(std::string_view path)
{
    size_t lastSlash = path.rfind('/');

    if (lastSlash == std::string::npos) {
        lastSlash = path.rfind('\\');
    }

    return lastSlash;
}

std::string_view FileIO::extractDirectoryFromPath(std::string_view path)
{
    size_t lastSlash = indexOfLashSlash(path);
    if (lastSlash == std::string::npos) {
        return "";
    }

    // Remove filename, keep the final '/'
    path.remove_suffix(path.length() - lastSlash - 1);
    return path;
}


std::string_view FileIO::extractFileNameFromPath(std::string_view path)
{
    size_t lastSlash = indexOfLashSlash(path);
    if (lastSlash == std::string::npos) {
        return "";
    }

    path.remove_prefix(lastSlash + 1);
    return path;
}

std::string_view FileIO::removeExtensionFromPath(std::string_view path)
{
    size_t lastDot = path.find_last_of('.');
    if (lastDot != std::string::npos) {
        return path.substr(0, lastDot);
    } else {
        return path;
    }
}

std::string FileIO::normalizePath(std::string_view absolutePath)
{
    std::string normalizedPath = std::string(absolutePath);

    for (char& c : normalizedPath) {
        if (c == '\\') {
            c = '/';
        }
    }

    size_t idxOfAssetsDir = normalizedPath.find("/assets/");
    if (idxOfAssetsDir != std::string::npos) {
        normalizedPath = normalizedPath.substr(idxOfAssetsDir + 1);
    }

    return normalizedPath;
}

uint8_t* FileIO::readBinaryDataFromFileRawPtr(const std::string& filePath, size_t* outSize)
{
    SCOPED_PROFILE_ZONE();

    // Open file as binary and immediately seek to the end
    std::ifstream file(filePath, std::ios::ate | std::ios::binary);
    if (!file.is_open())
        return nullptr;

    size_t dataSize = file.tellg();
    uint8_t* data = reinterpret_cast<uint8_t*>(malloc(dataSize));

    file.seekg(0);
    file.read(reinterpret_cast<char*>(data), dataSize);

    file.close();

    if (outSize != nullptr) {
        *outSize = dataSize;
    }

    return data;
}

void FileIO::writeTextDataToFile(const std::string& filePath, const std::string& text)
{
    const char* textData = text.data();
    size_t sizeInBytes = text.size();
    writeBinaryDataToFile(filePath, textData, sizeInBytes);
}

void FileIO::writeBinaryDataToFile(const std::string& filePath, const char* data, size_t size)
{
    SCOPED_PROFILE_ZONE();

    ensureDirectoryForFile(filePath);

    std::ofstream file;
    file.open(filePath, std::ios::out | std::ios::trunc | std::ios::binary);

    if (!file.is_open()) {
        ARKOSE_LOG(Fatal, "Could not create file '{}' for writing binary data", filePath);
        return;
    }

    file.write(data, size);
    file.close();
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
    ARKOSE_LOG(Fatal, "Error parsing <{}> in {} file '{}'", token, m_fileType, m_path);
    return -1;
}

float FileIO::ParseContext::nextAsFloat(const char* token)
{
    auto maybeFloat = nextAsFloat();
    if (maybeFloat)
        return maybeFloat.value();
    ARKOSE_LOG(Fatal, "Error parsing <{}> in {} file '{}'", token, m_fileType, m_path);
    return -1.0f;
}
