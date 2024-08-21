#pragma once

#include "core/Logging.h"

enum class Brdf {
    Default = 0,
    Skin,
};

constexpr std::array<const char*, 2> BrdfNames = { "Default",
                                                   "Skin" };

inline const char* BrdfName(Brdf brdf)
{
    size_t idx = static_cast<size_t>(brdf);
    return BrdfNames[idx];
}

constexpr u64 Brdf_Min = 0;
constexpr u64 Brdf_Max = 1;

////////////////////////////////////////////////////////////////////////////////
// Serialization

#include <cereal/cereal.hpp>

template<class Archive>
std::string save_minimal(Archive const&, Brdf const& brdf)
{
    return BrdfName(brdf);
}

template<class Archive>
void load_minimal(Archive const&, Brdf& brdf, std::string const& value)
{
    if (value == BrdfName(Brdf::Default)) {
        brdf = Brdf::Default;
    } else if (value == BrdfName(Brdf::Skin)) {
        brdf = Brdf::Skin;
    } else {
        ARKOSE_LOG(Fatal, "Invalid BRDF name in asset '{}'", value);
    }
}
