#pragma once

#include "rendering/backend/base/Texture.h"
#include <ark/vector.h>
#include <ark/quaternion.h>
#include <cereal/cereal.hpp>

template<class Archive>
void serialize(Archive& archive, Texture::WrapModes& wrapModes)
{
    archive(cereal::make_nvp("u", wrapModes.u),
            cereal::make_nvp("v", wrapModes.v),
            cereal::make_nvp("w", wrapModes.w));
}

namespace ark {

    template<class Archive>
    void serialize(Archive& archive, vec2& v)
    {
        archive(cereal::make_nvp("x", v.x),
                cereal::make_nvp("y", v.y));
    }

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

    template<class Archive>
    void serialize(Archive& archive, quat& q)
    {
        archive(cereal::make_nvp("x", q.vec.x),
                cereal::make_nvp("y", q.vec.y),
                cereal::make_nvp("z", q.vec.z),
                cereal::make_nvp("w", q.w));
    }

}
