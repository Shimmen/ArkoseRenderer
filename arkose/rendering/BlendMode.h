#pragma once

#include "core/Types.h"
#include <array>

// For shader #define values
#include "shaders/shared/ShaderBlendMode.h"

enum class BlendMode {
    Opaque,
    Masked,
    Translucent,
};

constexpr std::array<const char*, 3> BlendModeNames = { "Opaque",
                                                        "Masked",
                                                        "Translucent" };

inline const char* BlendModeName(BlendMode blendMode)
{
    size_t idx = static_cast<size_t>(blendMode);
    return BlendModeNames[idx];
}

constexpr u64 BlendMode_Min = 0;
constexpr u64 BlendMode_Max = 2;

////////////////////////////////////////////////////////////////////////////////
// Serialization

#include <cereal/cereal.hpp>

template<class Archive>
std::string save_minimal(Archive const&, BlendMode const& blendMode)
{
    return BlendModeName(blendMode);
}

template<class Archive>
void load_minimal(Archive const&, BlendMode& blendMode, std::string const& value)
{
    if (value == BlendModeName(BlendMode::Opaque)) {
        blendMode = BlendMode::Opaque;
    } else if (value == BlendModeName(BlendMode::Masked)) {
        blendMode = BlendMode::Masked;
    } else if (value == BlendModeName(BlendMode::Translucent)) {
        blendMode = BlendMode::Translucent;
    }
}