#pragma once

#include "Plane.h"
#include "Sphere.h"

namespace geometry {

struct Frustum {
public:
    Frustum() = default;
    static Frustum createFromProjectionMatrix(mat4);

    bool includesSphere(const Sphere&);

private:
    explicit Frustum(Plane planes[6]);

    // NOTE: normal of planes are pointing outwards!
    Plane m_planes[6];
};

}
