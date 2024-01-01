#include "Frustum.h"

#include "utility/Profiling.h"
#include "core/Assert.h"

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
        ARKOSE_ASSERT(!planes[i].isDegenerate());
        m_planes[i] = planes[i];
    }
}

bool Frustum::isPointInside(vec3 point) const
{
    for (Plane const& plane : m_planes) {
        float distance = dot(plane.normal(), point) + plane.distance();
        if (distance > 0.0f) {
            return false;
        }
    }
    return true;
}

bool Frustum::includesSphere(const Sphere& sphere) const
{
    //SCOPED_PROFILE_ZONE();

    for (const Plane& plane : m_planes) {
        float distance = dot(plane.normal(), sphere.center()) + plane.distance();
        if (distance > sphere.radius())
            return false;
    }

    return true;
}

bool Frustum::includesAABB(ark::aabb3 const& aabb) const
{
    //SCOPED_PROFILE_ZONE();

    vec3 corners[8] = { /*vec3(aabb.min.x, aabb.min.y, aabb.min.z)*/ aabb.min,
                        vec3(aabb.min.x, aabb.min.y, aabb.max.z),
                        vec3(aabb.min.x, aabb.max.y, aabb.min.z),
                        vec3(aabb.min.x, aabb.max.y, aabb.max.z),
                        vec3(aabb.max.x, aabb.min.y, aabb.min.z),
                        vec3(aabb.max.x, aabb.min.y, aabb.max.z),
                        vec3(aabb.max.x, aabb.max.y, aabb.min.z),
                        /*vec3(aabb.max.x, aabb.max.y, aabb.max.z)*/ aabb.max };

    for (vec3 const& corner : corners) {
        if (isPointInside(corner)) {
            return true;
        }
    }

    return false;
}

Plane const& Frustum::plane(size_t idx) const
{
    ARKOSE_ASSERT(idx < 6);
    return m_planes[idx];
}

const Plane* Frustum::rawPlaneData(size_t* outByteSize) const
{
    if (outByteSize)
        *outByteSize = 6 * sizeof(Plane);
    return m_planes;
}

}
