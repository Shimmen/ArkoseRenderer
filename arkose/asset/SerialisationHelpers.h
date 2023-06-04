#pragma once

#include <ark/aabb.h>
#include <ark/vector.h>
#include <ark/quaternion.h>
#include <cereal/cereal.hpp>

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

    template<class Archive, typename T>
    void serialize(Archive& archive, ark::tvec4<T>& v)
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

    template<class Archive>
    void serialize(Archive& archive, aabb3& aabb)
    {
        archive(cereal::make_nvp("min", aabb.min),
                cereal::make_nvp("max", aabb.max));
    }

    template<class Archive>
    void serialize(Archive& archive, mat4& m)
    {
        archive(cereal::make_nvp("x", m.x),
                cereal::make_nvp("y", m.y),
                cereal::make_nvp("z", m.z),
                cereal::make_nvp("w", m.w));
    }

}
