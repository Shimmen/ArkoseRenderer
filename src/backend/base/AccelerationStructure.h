#pragma once

#include "backend/base/Buffer.h"
#include "backend/util/Common.h"
#include <moos/matrix.h>
#include <variant>
#include <vector>

// TODO: Avoid importing frontend stuff from backend
#include "rendering/scene/Transform.h" // for Transform object

enum class RTVertexFormat {
    XYZ32F
};

struct RTTriangleGeometry {
    const Buffer& vertexBuffer;
    uint32_t vertexCount;
    size_t vertexOffset;
    size_t vertexStride;
    RTVertexFormat vertexFormat;

    const Buffer& indexBuffer;
    uint32_t indexCount;
    size_t indexOffset;
    IndexType indexType;

    mat4 transform;
};

struct RTAABBGeometry {
    const Buffer& aabbBuffer;
    size_t aabbStride;
};

class RTGeometry {
public:
    RTGeometry(RTTriangleGeometry);
    RTGeometry(RTAABBGeometry);

    bool hasTriangles() const;
    bool hasAABBs() const;

    const RTTriangleGeometry& triangles() const;
    const RTAABBGeometry& aabbs() const;

private:
    std::variant<RTTriangleGeometry, RTAABBGeometry> m_internal;
};

class BottomLevelAS : public Resource {
public:
    BottomLevelAS() = default;
    BottomLevelAS(Backend&, std::vector<RTGeometry>);

    [[nodiscard]] const std::vector<RTGeometry>& geometries() const;

private:
    std::vector<RTGeometry> m_geometries {};
};

struct RTGeometryInstance {
    const BottomLevelAS& blas;
    const Transform& transform;
    uint32_t shaderBindingTableOffset;
    uint32_t customInstanceId;
    uint8_t hitMask;
};

class TopLevelAS : public Resource {
public:
    TopLevelAS() = default;
    TopLevelAS(Backend&, std::vector<RTGeometryInstance>);

    [[nodiscard]] const std::vector<RTGeometryInstance>& instances() const;
    [[nodiscard]] uint32_t instanceCount() const;

private:
    std::vector<RTGeometryInstance> m_instances {};
};