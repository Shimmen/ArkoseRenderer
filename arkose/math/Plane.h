#pragma once

#include <ark/vector.h>

namespace geometry {

struct Plane {

    Plane() = default;
    Plane(vec4 vector)
        : Plane(vector.xyz(), vector.w)
    {
    }

    Plane(vec3 normal, float distance)
        : m_normal(normal)
        , m_distance(distance)
    {
        float len = length(normal);
        m_normal /= len;
        m_distance /= len;
    }

    const vec3& normal() const { return m_normal; }
    float distance() const { return m_distance; }

    bool isDegenerate() const
    {
        return length(m_normal) < 1e-6f;
    }

    vec3 m_normal {};
    float m_distance {};
};

}
