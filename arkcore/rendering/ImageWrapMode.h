#pragma once

#include "core/Assert.h"
#include "utility/Hash.h"

enum class ImageWrapMode {
    Repeat,
    MirroredRepeat,
    ClampToEdge,
};

static constexpr std::array<const char*, 3> ImageWrapModeNames = { "Repeat",
                                                                   "MirroredRepeat",
                                                                   "ClampToEdge" };

static inline const char* ImageWrapModeName(ImageWrapMode wrapMode)
{
    size_t idx = static_cast<size_t>(wrapMode);
    return ImageWrapModeNames[idx];
}

static constexpr u64 ImageWrapMode_Min = 0;
static constexpr u64 ImageWrapMode_Max = 2;

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

template<class Archive>
std::string save_minimal(Archive const&, ImageWrapMode const& wrapMode)
{
    return ImageWrapModeName(wrapMode);
}

template<class Archive>
void load_minimal(Archive const&, ImageWrapMode& wrapMode, std::string const& value)
{
    if (value == ImageWrapModeName(ImageWrapMode::Repeat)) {
        wrapMode = ImageWrapMode::Repeat;
    } else if (value == ImageWrapModeName(ImageWrapMode::MirroredRepeat)) {
        wrapMode = ImageWrapMode::MirroredRepeat;
    } else if (value == ImageWrapModeName(ImageWrapMode::ClampToEdge)) {
        wrapMode = ImageWrapMode::ClampToEdge;
    } else {
        ASSERT_NOT_REACHED();
    }
}

template<class Archive>
void serialize(Archive& archive, ImageWrapModes& wrapModes)
{
    archive(cereal::make_nvp("u", wrapModes.u),
            cereal::make_nvp("v", wrapModes.v),
            cereal::make_nvp("w", wrapModes.w));
}
