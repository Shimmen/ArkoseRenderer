#include "ImageBakeSpec.h"

#include "utility/FileIO.h"

bool ImageBakeSpec::writeToFile(std::filesystem::path const& filePath) const
{
    std::ofstream fileStream { filePath };
    if (!fileStream.is_open()) {
        return false;
    }

    {
        cereal::JSONOutputArchive archive(fileStream);
        archive(cereal::make_nvp("imgspec", *this));
    }

    fileStream.close();
    return true;
}

bool ImageBakeSpec::readFromFile(std::filesystem::path const& filePath)
{
    std::ifstream fileStream { filePath };
    if (!fileStream.is_open()) {
        return false;
    }

    {
        cereal::JSONInputArchive jsonArchive(fileStream);
        jsonArchive(*this);
    }

    return true;
}
