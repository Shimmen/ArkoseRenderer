#pragma once

#include "core/Logging.h"

enum class Brdf {
    Default = 0,
    Skin,
};

////////////////////////////////////////////////////////////////////////////////
// Serialization

#include <cereal/cereal.hpp>
#include <magic_enum/magic_enum.hpp>

template<class Archive>
std::string save_minimal(Archive const&, Brdf const& brdf)
{
    return std::string(magic_enum::enum_name(brdf));
}

template<class Archive>
void load_minimal(Archive const&, Brdf& brdf, std::string const& value)
{
    brdf = magic_enum::enum_cast<Brdf>(value).value();
}
