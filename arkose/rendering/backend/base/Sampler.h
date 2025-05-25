#pragma once

#include "rendering/ImageFilter.h"
#include "rendering/ImageWrapMode.h"
#include "rendering/backend/Resource.h"

class Sampler : public Resource {
public:

    enum class Mipmap {
        None,
        Nearest,
        Linear,
    };

    struct Description {
        ImageWrapModes wrapMode { ImageWrapModes::clampAllToEdge() };
        ImageFilter minFilter { ImageFilter::Nearest };
        ImageFilter magFilter { ImageFilter::Nearest };
        Mipmap mipmap { Mipmap::None };
    };

    Sampler() = default;
    Sampler(Backend&, Description&);

private:
    Description m_description;
};
