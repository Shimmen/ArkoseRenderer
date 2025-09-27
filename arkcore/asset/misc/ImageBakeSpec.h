#pragma once

#include "asset/ImageAsset.h"
#include <filesystem>

// Specifies metadata for how to bake an image
struct ImageBakeSpec {
    std::string inputImage;
    std::string targetImage;

    ImageType type { ImageType::Unknown };

    bool generateMipmaps { true };
    bool compress { true };

    template<class Archive>
    void serialize(Archive&);

    bool writeToFile(std::filesystem::path const& filePath) const;
    bool readFromFile(std::filesystem::path const& filePath);

    // eh hack, but works for now..
    std::filesystem::path selfPath;
};

////////////////////////////////////////////////////////////////////////////////
// Serialization

#include <cereal/cereal.hpp>
#include <cereal/types/string.hpp>

template<class Archive>
void ImageBakeSpec::serialize(Archive& archive)
{
    archive(CEREAL_NVP(inputImage));
    archive(CEREAL_NVP(targetImage));
    archive(CEREAL_NVP(type));
    archive(CEREAL_NVP(generateMipmaps));
    archive(CEREAL_NVP(compress));
}
