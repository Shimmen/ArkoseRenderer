#pragma once

#include "core/Types.h"
#include "rendering/backend/base/Buffer.h"
#include "rendering/backend/util/DrawCall.h"
#include "rendering/backend/util/IndexType.h"
#include "scene/Vertex.h"
#include <ark/copying.h>
#include <memory>
#include <optional>

class Backend;
class BottomLevelAS;
class MeshSegmentAsset;
class StaticMesh;
struct SkeletalMeshInstance;
struct StaticMeshSegment;

struct VertexAllocation {
    u32 firstVertex { 0 };
    u32 vertexCount { 0 };
    u32 firstIndex { 0 };
    u32 indexCount { 0 };

    i32 firstSkinningVertex { -1 };
    bool hasSkinningData() const { return firstSkinningVertex >= 0; }

    i32 firstVelocityVertex { -1 };
    bool hasVelocityData() const { return firstVelocityVertex >= 0; }

    bool isValid() const { return vertexCount > 0; }
    bool hasIndices() const { return indexCount > 0; }

    DrawCallDescription asDrawCallDescription() const;
};

class VertexManager {
public:
    explicit VertexManager(Backend&);
    ~VertexManager();

    ARK_NON_COPYABLE(VertexManager);

    VertexAllocation allocateMeshDataForSegment(MeshSegmentAsset const&, bool includeIndices, bool includeSkinningData, bool includeVelocityData);
    bool uploadMeshData(StaticMesh&, bool includeIndices, bool includeSkinningData);

    bool createBottomLevelAccelerationStructure(StaticMesh&);
    std::unique_ptr<BottomLevelAS> createBottomLevelAccelerationStructure(VertexAllocation const&, BottomLevelAS const* copySource);

    IndexType indexType() const { return IndexType::UInt32; }
    Buffer const& indexBuffer() const { return *m_indexBuffer; }

    VertexLayout const& positionVertexLayout() const { return m_positionOnlyVertexLayout; }
    Buffer const& positionVertexBuffer() const { return *m_positionOnlyVertexBuffer; }
    Buffer& positionVertexBuffer() { return *m_positionOnlyVertexBuffer; }

    VertexLayout const& nonPositionVertexLayout() const { return m_nonPositionVertexLayout; }
    Buffer const& nonPositionVertexBuffer() const { return *m_nonPositionVertexBuffer; }
    Buffer& nonPositionVertexBuffer() { return *m_nonPositionVertexBuffer; }

    VertexLayout const& skinningDataVertexLayout() const { return m_skinningDataVertexLayout; }
    Buffer const& skinningDataVertexBuffer() const { return *m_skinningDataVertexBuffer; }

    VertexLayout const& velocityDataVertexLayout() const { return m_velocityDataVertexLayout; }
    Buffer const& velocityDataVertexBuffer() const { return *m_velocityDataVertexBuffer; }
    Buffer& velocityDataVertexBuffer() { return *m_velocityDataVertexBuffer; }

    // Max that can be loaded in the GPU at any time
    // TODO: Optimize these sizes!
    static constexpr size_t MaxLoadedVertices         = 5'000'000;
    static constexpr size_t MaxLoadedSkinningVertices = 10'000;
    static constexpr size_t MaxLoadedVelocityVertices = 10'000;
    static constexpr size_t MaxLoadedTriangles        = 10'000'000;
    static constexpr size_t MaxLoadedIndices          = 3 * MaxLoadedTriangles;

private:
    Backend* m_backend { nullptr };

    VertexLayout const m_positionOnlyVertexLayout { VertexComponent::Position3F };
    VertexLayout const m_nonPositionVertexLayout { VertexComponent::TexCoord2F,
                                                   VertexComponent::Normal3F,
                                                   VertexComponent::Tangent4F };
    VertexLayout const m_skinningDataVertexLayout { VertexComponent::JointIdx4U32,
                                                    VertexComponent::JointWeight4F };
    VertexLayout const m_velocityDataVertexLayout { VertexComponent::Velocity3F };

    std::unique_ptr<Buffer> m_indexBuffer { nullptr };
    u32 m_nextFreeIndex { 0 };

    std::unique_ptr<Buffer> m_positionOnlyVertexBuffer {};
    std::unique_ptr<Buffer> m_nonPositionVertexBuffer {};
    u32 m_nextFreeVertexIndex { 0 };

    std::unique_ptr<Buffer> m_skinningDataVertexBuffer {};
    u32 m_nextFreeSkinningVertexIndex { 0 };

    std::unique_ptr<Buffer> m_velocityDataVertexBuffer {};
    u32 m_nextFreeVelocityIndex { 0 };

    struct VertexUploadJob {
        MeshSegmentAsset const* asset { nullptr };
        StaticMeshSegment* target { nullptr };
        VertexAllocation allocation {};
    };

    void uploadMeshDataForAllocation(VertexUploadJob const&);
};
