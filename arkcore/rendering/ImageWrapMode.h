#pragma once

#include "utility/Hash.h"

enum class ImageWrapMode {
    Repeat,
    MirroredRepeat,
    ClampToEdge,
};

struct ImageWrapModes {
    ImageWrapMode u { ImageWrapMode::Repeat };
    ImageWrapMode v { ImageWrapMode::Repeat };
    ImageWrapMode w { ImageWrapMode::Repeat };

    ImageWrapModes() = default;

    constexpr ImageWrapModes(ImageWrapMode u, ImageWrapMode v)
        : u(u)
        , v(v)
        , w(ImageWrapMode::ClampToEdge)
    {
    }

    constexpr ImageWrapModes(ImageWrapMode u, ImageWrapMode v, ImageWrapMode w)
        : u(u)
        , v(v)
        , w(w)
    {
    }

    static constexpr ImageWrapModes repeatAll()
    {
        return {
            ImageWrapMode::Repeat,
            ImageWrapMode::Repeat,
            ImageWrapMode::Repeat
        };
    }

    static constexpr ImageWrapModes mirroredRepeatAll()
    {
        return {
            ImageWrapMode::MirroredRepeat,
            ImageWrapMode::MirroredRepeat,
            ImageWrapMode::MirroredRepeat
        };
    }

    static constexpr ImageWrapModes clampAllToEdge()
    {
        return {
            ImageWrapMode::ClampToEdge,
            ImageWrapMode::ClampToEdge,
            ImageWrapMode::ClampToEdge
        };
    }

    bool operator==(const ImageWrapModes& rhs) const
    {
        return u == rhs.u
            && v == rhs.v
            && w == rhs.w;
    }
};

namespace std {

    template<>
    struct hash<ImageWrapModes> {
        std::size_t operator()(const ImageWrapModes& wrapModes) const
        {
            auto uHash = std::hash<ImageWrapMode>()(wrapModes.u);
            auto vHash = std::hash<ImageWrapMode>()(wrapModes.v);
            auto wHash = std::hash<ImageWrapMode>()(wrapModes.w);
            return hashCombine(uHash, hashCombine(vHash, wHash));
        }
    };

}

////////////////////////////////////////////////////////////////////////////////
// Serialization

#include <cereal/cereal.hpp>
#include <magic_enum/magic_enum.hpp>

template<class Archive>
std::string save_minimal(Archive const&, ImageWrapMode const& wrapMode)
{
    return std::string(magic_enum::enum_name(wrapMode));
}

template<class Archive>
void load_minimal(Archive const&, ImageWrapMode& wrapMode, std::string const& value)
{
    wrapMode = magic_enum::enum_cast<ImageWrapMode>(value).value();
}

template<class Archive>
void serialize(Archive& archive, ImageWrapModes& wrapModes)
{
    archive(cereal::make_nvp("u", wrapModes.u),
            cereal::make_nvp("v", wrapModes.v),
            cereal::make_nvp("w", wrapModes.w));
}
