#pragma once

#include <mooslib/matrix.h>
#include <mooslib/vector.h>

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

private:
    vec3 m_center {};
    float m_radius {};
};

}
