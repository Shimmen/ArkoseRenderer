#include "Frustum.h"

#include "utility/util.h"

namespace geometry {

Frustum Frustum::createFromProjectionMatrix(mat4 M)
{
    mat4 m = transpose(M);
    Plane planes[] = { -(m[3] + m[0]),
                       -(m[3] - m[0]),
                       -(m[3] + m[1]),
                       -(m[3] - m[1]),
                       -(m[3] + m[2]),
                       -(m[3] - m[2]) };
    return Frustum(planes);
}

Frustum::Frustum(Plane planes[6])
{
    for (size_t i = 0; i < 6; ++i) {
        ASSERT(!planes[i].isDegenerate());
        m_planes[i] = planes[i];
    }
}

bool Frustum::includesSphere(const Sphere& sphere)
{
    for (const Plane& plane : m_planes) {
        float distance = dot(plane.normal(), sphere.center()) + plane.distance();
        if (distance > sphere.radius())
            return false;
    }

    return true;
}

}
