#pragma once

#include "core/Types.h"
#include "rendering/backend/base/Buffer.h"
#include "rendering/backend/util/DrawCall.h"
#include "rendering/backend/util/IndexType.h"
#include "scene/Vertex.h"
#include <memory>
#include <optional>

class Backend;
class BottomLevelAS;
class MeshSegmentAsset;
class SkeletalMeshInstance;
class StaticMesh;
class StaticMeshSegment;

struct VertexAllocation {
    u32 firstVertex { 0 };
    u32 vertexCount { 0 };
    u32 firstIndex { 0 };
    u32 indexCount { 0 };

    i32 firstSkinningVertex { -1 };
    bool hasSkinningData() const { return firstSkinningVertex >= 0; }

    bool isValid() const { return vertexCount > 0; }
    bool hasIndices() const { return indexCount > 0; }

    DrawCallDescription asDrawCallDescription() const;
};

class VertexManager {
public:
    explicit VertexManager(Backend&);
    ~VertexManager();

    bool allocateMeshData(StaticMesh&, bool includeSkinningData);
    bool uploadMeshData(StaticMesh&, bool includeSkinningData);

    bool createBottomLevelAccelerationStructure(StaticMesh&);

    // TODO: Maybe don't keep here?!
    void skinSkeletalMeshInstance(SkeletalMeshInstance&);

    IndexType indexType() const { return IndexType::UInt32; }
    Buffer const& indexBuffer() const { return *m_indexBuffer; }

    VertexLayout const& positionVertexLayout() const { return m_positionOnlyVertexLayout; }
    Buffer const& positionVertexBuffer() const { return *m_positionOnlyVertexBuffer; }

    VertexLayout const& nonPositionVertexLayout() const { return m_nonPositionVertexLayout; }
    Buffer const& nonPositionVertexBuffer() const { return *m_nonPositionVertexBuffer; }

    VertexLayout const& skinningDataVertexLayout() const { return m_skinningDataVertexLayout; }
    Buffer const& skinningDataVertexBuffer() const { return *m_skinningDataVertexBuffer; }

private:
    Backend* m_backend { nullptr };

    VertexLayout const m_positionOnlyVertexLayout { VertexComponent::Position3F };
    VertexLayout const m_nonPositionVertexLayout { VertexComponent::TexCoord2F,
                                                   VertexComponent::Normal3F,
                                                   VertexComponent::Tangent4F };
    VertexLayout const m_skinningDataVertexLayout { VertexComponent::JointIdx4U32,
                                                    VertexComponent::JointWeight4F };

    std::unique_ptr<Buffer> m_indexBuffer { nullptr };
    u32 m_nextFreeIndex { 0 };

    std::unique_ptr<Buffer> m_positionOnlyVertexBuffer {};
    std::unique_ptr<Buffer> m_nonPositionVertexBuffer {};
    u32 m_nextFreeVertexIndex { 0 };

    std::unique_ptr<Buffer> m_skinningDataVertexBuffer {};
    u32 m_nextFreeSkinningVertexIndex { 0 };

    struct VertexUploadJob {
        MeshSegmentAsset const* asset { nullptr };
        StaticMeshSegment* target { nullptr };
        VertexAllocation allocation {};
    };

    std::optional<VertexAllocation> allocateMeshDataForSegment(MeshSegmentAsset const&, bool includeSkinningData);
    void uploadMeshDataForAllocation(VertexUploadJob const&);

    std::unique_ptr<BottomLevelAS> createBottomLevelAccelerationStructure(StaticMeshSegment const&);
};
