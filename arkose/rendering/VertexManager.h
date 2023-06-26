#pragma once

#include "core/Types.h"
#include "rendering/backend/base/Buffer.h"
#include "rendering/backend/util/IndexType.h"
#include "scene/Vertex.h"
#include <memory>
#include <optional>

class Backend;
class MeshSegmentAsset;
class SkeletalMeshInstance;
class StaticMesh;
class StaticMeshSegment;

struct VertexAllocation {
    u32 firstVertex { 0 };
    u32 vertexCount { 0 };
    u32 firstIndex { 0 };
    u32 indexCount { 0 };

    bool isValid() const { return vertexCount > 0; }
    bool hasIndices() const { return indexCount > 0; }
};

class VertexManager {
public:
    explicit VertexManager(Backend&);
    ~VertexManager();

    bool allocateMeshData(StaticMesh&);
    bool uploadMeshData(StaticMesh&);

    // TODO: Maybe don't keep here?!
    void skinSkeletalMeshInstance(SkeletalMeshInstance&);

    IndexType indexType() const { return IndexType::UInt32; }
    Buffer const& indexBuffer() const { return *m_indexBuffer; }

    VertexLayout const& positionVertexLayout() const { return m_positionOnlyVertexLayout; }
    Buffer const& positionVertexBuffer() const { return *m_positionOnlyVertexBuffer; }

    VertexLayout const& nonPositionVertexLayout() const { return m_nonPositionVertexLayout; }
    Buffer const& nonPositionVertexBuffer() const { return *m_nonPositionVertexBuffer; }

private:
    VertexLayout m_positionOnlyVertexLayout { VertexComponent::Position3F };
    VertexLayout m_nonPositionVertexLayout { VertexComponent::TexCoord2F,
                                             VertexComponent::Normal3F,
                                             VertexComponent::Tangent4F };

    std::unique_ptr<Buffer> m_indexBuffer { nullptr };
    u32 m_nextFreeIndex { 0 };

    std::unique_ptr<Buffer> m_positionOnlyVertexBuffer {};
    std::unique_ptr<Buffer> m_nonPositionVertexBuffer {};
    u32 m_nextFreeVertexIndex { 0 };

    struct VertexUploadJob {
        MeshSegmentAsset const* asset { nullptr };
        StaticMeshSegment* target { nullptr };
        VertexAllocation allocation {};
    };

    std::optional<VertexAllocation> allocateMeshDataForSegment(MeshSegmentAsset const&);
    void uploadMeshDataForAllocation(VertexUploadJob const&);
};
