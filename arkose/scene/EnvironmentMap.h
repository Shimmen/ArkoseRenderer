#pragma once

struct EnvironmentMap {
    std::string assetPath {};
    float brightnessFactor { 1.0f };
};

////////////////////////////////////////////////////////////////////////////////
// Serialization

#include <cereal/types/string.hpp>

template<class Archive>
void serialize(Archive& archive, EnvironmentMap& environmentMap)
{
    archive(cereal::make_nvp("assetPath", environmentMap.assetPath));
    archive(cereal::make_nvp("brightnessFactor", environmentMap.brightnessFactor));
}
