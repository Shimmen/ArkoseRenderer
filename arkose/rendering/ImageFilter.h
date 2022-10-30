#pragma once

enum class ImageFilter {
    Nearest,
    Linear,
};

constexpr std::array<const char*, 2> ImageFilterNames = { "Nearest", "Linear" };
inline const char* ImageFilterName(ImageFilter imageFilter)
{
    size_t idx = static_cast<size_t>(imageFilter);
    return ImageFilterNames[idx];
}

constexpr u64 ImageFilter_Min = 0;
constexpr u64 ImageFilter_Max = 1;

////////////////////////////////////////////////////////////////////////////////
// Serialization

#include <cereal/cereal.hpp>

template<class Archive>
std::string save_minimal(Archive const&, ImageFilter const& imageFilter)
{
    return ImageFilterName(imageFilter);
}

template<class Archive>
void load_minimal(Archive const&, ImageFilter& imageFilter, std::string const& value)
{
    if (value == ImageFilterName(ImageFilter::Nearest)) {
        imageFilter = ImageFilter::Nearest;
    } else if (value == ImageFilterName(ImageFilter::Linear)) {
        imageFilter = ImageFilter::Linear;
    }
}
