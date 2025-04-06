#include "FileIO.h"

#include "core/Assert.h"
#include "core/Logging.h"

bool FileIO::fileReadable(std::filesystem::path const& filePath)
{
    return std::filesystem::exists(filePath);
}

void FileIO::ensureDirectory(std::filesystem::path const& directoryPath)
{
    std::filesystem::create_directories(directoryPath);
}

void FileIO::ensureDirectoryForFile(std::filesystem::path const& filePath)
{
    if (filePath.has_stem()) {
        return ensureDirectory(filePath.parent_path());
    } else {
        return ensureDirectory(filePath);
    }
}

void FileIO::writeTextDataToFile(std::filesystem::path const& filePath, std::string_view text)
{
    SCOPED_PROFILE_ZONE();

    ensureDirectoryForFile(filePath);

    std::ofstream file;
    file.open(filePath, std::ios::out | std::ios::trunc);

    if (!file.is_open()) {
        ARKOSE_LOG(Fatal, "Could not create file '{}' for writing text data", filePath);
        return;
    }

    file.write(text.data(), text.size());
    file.close();
}

void FileIO::writeBinaryDataToFile(std::filesystem::path const& filePath, std::byte const* data, size_t size)
{
    SCOPED_PROFILE_ZONE();

    ensureDirectoryForFile(filePath);

    std::ofstream file;
    file.open(filePath, std::ios::out | std::ios::trunc | std::ios::binary);

    if (!file.is_open()) {
        ARKOSE_LOG(Fatal, "Could not create file '{}' for writing binary data", filePath);
        return;
    }

    file.write(reinterpret_cast<char const*>(data), size);
    file.close();
}

std::optional<std::string> FileIO::readFile(std::filesystem::path const& filePath)
{
    SCOPED_PROFILE_ZONE();

    std::ifstream file(filePath, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        return {};
    }

    std::string contents {};

    size_t sizeInBytes = file.tellg();
    contents.reserve(sizeInBytes);
    file.seekg(0, std::ios::beg);

    contents.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());

    file.close();
    return contents;
}

bool FileIO::readFileLineByLine(std::filesystem::path const& filePath, std::function<LoopAction(std::string const& line)> lineCallback)
{
    std::ifstream file(filePath);

    if (!file.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        LoopAction nextAction = lineCallback(line);
        if (nextAction == LoopAction::Break) {
            break;
        }
    }

    return true;
}
