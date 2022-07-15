#pragma once

#include "rendering/backend/base/Buffer.h"
#include "rendering/backend/util/IndexType.h"
#include <ark/matrix.h>
#include <variant>
#include <vector>

// TODO: Avoid importing frontend stuff from backend
#include "scene/Transform.h" // for Transform object

class UploadBuffer;

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

enum class AccelerationStructureBuildType {
    FullBuild,
    Update,
};

class BottomLevelAS : public Resource {
public:
    BottomLevelAS() = default;
    BottomLevelAS(Backend&, std::vector<RTGeometry>);

    [[nodiscard]] const std::vector<RTGeometry>& geometries() const;

    size_t sizeInMemory() { return m_sizeInMemory; }

protected:
    size_t m_sizeInMemory { SIZE_MAX };

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
    TopLevelAS(Backend&, uint32_t maxInstanceCount);

    virtual void updateInstanceDataWithUploadBuffer(const std::vector<RTGeometryInstance>&, UploadBuffer&) = 0;

    [[nodiscard]] uint32_t instanceCount() const { return m_instanceCount; }
    [[nodiscard]] uint32_t maxInstanceCount() const { return m_maxInstanceCount; }

protected:
    void updateCurrentInstanceCount(uint32_t newInstanceCount);

private:
    uint32_t m_instanceCount { 0 };
    uint32_t m_maxInstanceCount { 0 };
};
