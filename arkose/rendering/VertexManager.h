#pragma once

#include "core/Types.h"
#include "rendering/VertexAllocation.h"
#include "rendering/backend/base/Buffer.h"
#include "rendering/backend/util/DrawCall.h"
#include "rendering/backend/util/IndexType.h"
#include "rendering/StaticMesh.h"
#include "scene/Vertex.h"
#include <ark/copying.h>
#include <memory>
#include <optional>
#include <unordered_set>

// Shader headers
#include "shaders/shared/SceneData.h"

class Backend;
class BottomLevelAS;
class CommandList;
class MeshSegmentAsset;
struct SkeletalMeshInstance;
struct StaticMeshSegment;

class VertexManager {
public:
    VertexManager(Backend&, GpuScene&);
    ~VertexManager();

    ARK_NON_COPYABLE(VertexManager);

    void registerForStreaming(StaticMesh&, bool includeIndices, bool includeSkinningData);
    //void unregisterFromStreaming(StaticMesh&); TODO!

    bool allocateSkeletalMeshInstance(SkeletalMeshInstance&);

    void processMeshStreaming(CommandList&, std::unordered_set<StaticMeshHandle>& updatedMeshes);

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

    std::vector<ShaderMeshlet> const& meshlets() const { return m_meshlets; }
    Buffer const& meshletBuffer() const { return *m_meshletBuffer; }
    Buffer& meshletBuffer() { return *m_meshletBuffer; }

    Buffer const& meshletVertexIndirectionBuffer() const { return *m_meshletVertexIndirectionBuffer; }
    Buffer& meshletVertexIndirectionBuffer() { return *m_meshletVertexIndirectionBuffer; }

    IndexType meshletIndexType() const { return IndexType::UInt32; }
    Buffer const& meshletIndexBuffer() const { return *m_meshletIndexBuffer; }
    Buffer& meshletIndexBuffer() { return *m_meshletIndexBuffer; }

    // Max that can be loaded in the GPU at any time
    // TODO: Optimize these sizes!
    static constexpr size_t MaxLoadedVertices         = 5'000'000;
    static constexpr size_t MaxLoadedSkinningVertices = 10'000;
    static constexpr size_t MaxLoadedVelocityVertices = 10'000;
    static constexpr size_t MaxLoadedTriangles        = 10'000'000;
    static constexpr size_t MaxLoadedIndices          = 3 * MaxLoadedTriangles;

    static constexpr size_t MaxLoadedMeshlets         = MaxLoadedTriangles / 124;

    static constexpr size_t UploadBufferSize          = 4 * 1024 * 1024;

    u32 numAllocatedIndices() const { return m_nextFreeIndex; }
    u32 numAllocatedVertices() const { return m_nextFreeVertexIndex; }
    u32 numAllocatedSkinningVertices() const { return m_nextFreeSkinningVertexIndex; }
    u32 numAllocatedVelocityVertices() const { return m_nextFreeVelocityIndex; }

private:
    Backend* m_backend { nullptr };
    GpuScene* m_scene { nullptr };

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

    std::unique_ptr<Buffer> m_meshletVertexIndirectionBuffer {};
    u32 m_nextFreeMeshletIndirIndex { 0 };

    std::unique_ptr<Buffer> m_meshletIndexBuffer {};
    u32 m_nextFreeMeshletIndexBufferIndex { 0 };

    std::vector<ShaderMeshlet> m_meshlets {};
    std::unique_ptr<Buffer> m_meshletBuffer {};
    u32 m_nextFreeMeshletIndex { 0 };

    bool allocateVertexDataForMesh(StaticMesh&, bool includeIndices, bool includeSkinningData, bool includeVelocityData);
    VertexAllocation allocateMeshDataForSegment(MeshSegmentAsset const&, bool includeIndices, bool includeSkinningData, bool includeVelocityData);

    struct VertexUploadJob {
        MeshSegmentAsset const* asset { nullptr };
        StaticMeshSegment* target { nullptr };
        VertexAllocation allocation {};
    };

    std::unique_ptr<UploadBuffer> m_uploadBuffer {};

    void uploadMeshDataForAllocation(VertexUploadJob const&);

    bool streamMeshletData(StaticMesh&, std::unordered_set<StaticMeshHandle>& updatedMeshes);
    std::optional<MeshletView> streamMeshletDataForSegment(StaticMeshSegment const&);

    enum class MeshStreamingState {
        PendingAllocation = 0,
        LoadingData,
        // TODO:
        //StreamingVertexData,
        //StreamingIndexData,
        StreamingMeshletData,
        CreatingBLAS,
        // TODO:
        //CompactingBLAS,
        Loaded,
    };

    struct StreamingMesh {
        StaticMesh* mesh;
        MeshStreamingState state;

        bool includeIndices { false };
        bool includeSkinningData { false };
        bool includeVelocityData { false };

        u32 nextLOD { 0 };
        u32 nextSegment { 0 };
        u32 nextVertex { 0 };
        u32 nextIndex { 0 };
    };

    // List of all streaming meshes that are not in the Loaded state
    std::vector<StreamingMesh> m_activeStreamingMeshes {};
    // List of all streaming meshes that are done streaming and in the Loaded state
    std::vector<StreamingMesh> m_idleStreamingMeshes {};
};
