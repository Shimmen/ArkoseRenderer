#pragma once

#include "rendering/backend/base/Texture.h"
#include <ark/vector.h>
#include <cereal/macros.hpp>

template<class Archive>
void serialize(Archive& archive, Texture::WrapModes& wrapModes)
{
    archive(cereal::make_nvp("u", wrapModes.u),
            cereal::make_nvp("v", wrapModes.v),
            cereal::make_nvp("w", wrapModes.w));
}

namespace ark {

    template<class Archive>
    void serialize(Archive& archive, vec3& v)
    {
        archive(cereal::make_nvp("x", v.x),
                cereal::make_nvp("y", v.y),
                cereal::make_nvp("z", v.z));
    }

    template<class Archive>
    void serialize(Archive& archive, vec4& v)
    {
        archive(cereal::make_nvp("x", v.x),
                cereal::make_nvp("y", v.y),
                cereal::make_nvp("z", v.z),
                cereal::make_nvp("w", v.w));
    }

}
