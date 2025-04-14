#pragma once

enum class ImageFilter {
    Nearest,
    Linear,
};

////////////////////////////////////////////////////////////////////////////////
// Serialization

#include <cereal/cereal.hpp>
#include <magic_enum/magic_enum.hpp>

template<class Archive>
std::string save_minimal(Archive const&, ImageFilter const& imageFilter)
{
    return std::string(magic_enum::enum_name(imageFilter));
}

template<class Archive>
void load_minimal(Archive const&, ImageFilter& imageFilter, std::string const& value)
{
    imageFilter = magic_enum::enum_cast<ImageFilter>(value).value();
}
