#include "AccelerationStructure.h"

#include "backend/util/UploadBuffer.h"

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

TopLevelAS::TopLevelAS(Backend& backend, uint32_t maxInstanceCountIn)
    : Resource(backend)
    , m_maxInstanceCount(maxInstanceCountIn)
{
}

void TopLevelAS::updateCurrentInstanceCount(uint32_t newInstanceCount)
{
    ASSERT(newInstanceCount > 0);
    ASSERT(newInstanceCount <= maxInstanceCount());

    m_instanceCount = newInstanceCount;
}
