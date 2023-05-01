#pragma once

#include "utility/Profiling.h"
#include <ark/matrix.h>
#include <ark/vector.h>
#include <cereal/cereal.hpp>

namespace geometry {

struct Sphere {

    Sphere() = default;

    Sphere(vec3 center, float radius)
        : m_center(center)
        , m_radius(radius)
    {
    }

    inline Sphere transformed(mat4 M) const
    {
        SCOPED_PROFILE_ZONE();

        mat4 Mt = transpose(M);
        float scaleX2 = length2(Mt.x.xyz());
        float scaleY2 = length2(Mt.y.xyz());
        float scaleZ2 = length2(Mt.z.xyz());

        float newRadius = m_radius * std::sqrt(std::max({ scaleX2, scaleY2, scaleZ2 }));
        vec3 newCenter = vec3(M * vec4(m_center, 1.0f));

        return Sphere(newCenter, newRadius);
    }

    const vec3& center() const { return m_center; }
    float radius() const { return m_radius; }

    bool isDegenerate() const
    {
        return std::abs(m_radius) < 1e-6f;
    }

    vec4 asVec4() const
    {
        return vec4(m_center, m_radius);
    }

    template<class Archive>
    void serialize(Archive& archive)
    {
        archive(cereal::make_nvp("center", m_center),
                cereal::make_nvp("radius", m_radius));
    }

private:
    vec3 m_center {};
    float m_radius {};
};

}
