#pragma once

#include "core/Types.h"

enum class BlendMode {
    Opaque,
    Masked,
    Translucent,
};

////////////////////////////////////////////////////////////////////////////////
// Serialization

#include <cereal/cereal.hpp>
#include <magic_enum/magic_enum.hpp>

template<class Archive>
std::string save_minimal(Archive const&, BlendMode const& blendMode)
{
    return std::string(magic_enum::enum_name(blendMode));
}

template<class Archive>
void load_minimal(Archive const&, BlendMode& blendMode, std::string const& value)
{
    blendMode = magic_enum::enum_cast<BlendMode>(value).value();
}
