#include "AccelerationStructure.h"

RTGeometry::RTGeometry(RTTriangleGeometry triangles)
    : m_internal(triangles)
{
}

RTGeometry::RTGeometry(RTAABBGeometry aabbs)
    : m_internal(aabbs)
{
}

bool RTGeometry::hasTriangles() const
{
    return std::holds_alternative<RTTriangleGeometry>(m_internal);
}

bool RTGeometry::hasAABBs() const
{
    return std::holds_alternative<RTAABBGeometry>(m_internal);
}

const RTTriangleGeometry& RTGeometry::triangles() const
{
    return std::get<RTTriangleGeometry>(m_internal);
}

const RTAABBGeometry& RTGeometry::aabbs() const
{
    return std::get<RTAABBGeometry>(m_internal);
}

BottomLevelAS::BottomLevelAS(Backend& backend, std::vector<RTGeometry> geometries)
    : Resource(backend)
    , m_geometries(geometries)
{
}

const std::vector<RTGeometry>& BottomLevelAS::geometries() const
{
    return m_geometries;
}

TopLevelAS::TopLevelAS(Backend& backend, std::vector<RTGeometryInstance> instances)
    : Resource(backend)
    , m_instances(instances)
{
}

const std::vector<RTGeometryInstance>& TopLevelAS::instances() const
{
    return m_instances;
}

uint32_t TopLevelAS::instanceCount() const
{
    return static_cast<uint32_t>(m_instances.size());
}
