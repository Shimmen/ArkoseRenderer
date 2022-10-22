#pragma once

#include "ark/aabb.h"
#include "Plane.h"
#include "Sphere.h"

namespace geometry {

struct Frustum {
public:
    Frustum() = default;
    static Frustum createFromProjectionMatrix(mat4);

    bool isPointInside(vec3 point) const;

    bool includesSphere(const Sphere&) const;
    bool includesAABB(ark::aabb3 const&) const;

    const Plane* rawPlaneData(size_t* outByteSize) const;

private:
    explicit Frustum(Plane planes[6]);

    // NOTE: normal of planes are pointing outwards!
    Plane m_planes[6];
};

}
