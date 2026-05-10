#pragma once

#include "core/Types.h"
#include "rendering/VertexAllocation.h"
#include "rendering/backend/base/Buffer.h"
#include "rendering/backend/util/DrawCall.h"
#include "rendering/backend/util/IndexType.h"
#include "rendering/HairMesh.h"
#include "rendering/StaticMesh.h"
#include "rendering/Vertex.h"
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
class UploadBuffer;
struct SkeletalMeshInstance;
struct StaticMeshSegment;

class VertexManager {
public:
    VertexManager(Backend&, GpuScene&);
    ~VertexManager();

    ARK_NON_COPYABLE(VertexManager);

    void registerForStreaming(StaticMesh&, bool includeIndices, bool includeSkinningData, bool includeMorphData);
    //void unregisterFromStreaming(StaticMesh&); TODO!

    bool allocateSkeletalMeshInstance(SkeletalMeshInstance&, CommandList&);
    //void deallocateSkeletalMeshInstance(SkeletalMeshInstance&); // TODO!

    void registerForStreaming(HairMesh&);

    void processMeshStreaming(CommandList&, UploadBuffer&, std::unordered_set<StaticMeshHandle>& updatedMeshes);
    void processHairStreaming(CommandList&, UploadBuffer&);

    void drawUI() const;

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

    VertexLayout const& morphTargetVertexLayout() const { return m_morphTargetVertexLayout; }
    Buffer const& morphTargetVertexBuffer() const { return *m_morphTargetVertexBuffer; }
    Buffer& morphTargetVertexBuffer() { return *m_morphTargetVertexBuffer; }

    VertexLayout const& hairPositionVertexLayout() const { return m_hairPositionVertexLayout; }
    Buffer const& hairPositionVertexBuffer() const { return *m_hairPositionVertexBuffer; }
    Buffer& hairPositionVertexBuffer() { return *m_hairPositionVertexBuffer; }

    VertexLayout const& hairAttributeVertexLayout() const { return m_hairAttributeVertexLayout; }
    Buffer const& hairAttributeVertexBuffer() const { return *m_hairAttributeVertexBuffer; }
    Buffer& hairAttributeVertexBuffer() { return *m_hairAttributeVertexBuffer; }

    std::vector<ShaderMeshlet> const& meshlets() const { return m_meshlets; }
    Buffer const& meshletBuffer() const { return *m_meshletBuffer; }
    Buffer& meshletBuffer() { return *m_meshletBuffer; }

    Buffer const& meshletVertexIndirectionBuffer() const { return *m_meshletVertexIndirectionBuffer; }
    Buffer& meshletVertexIndirectionBuffer() { return *m_meshletVertexIndirectionBuffer; }

    IndexType meshletIndexType() const { return IndexType::UInt32; }
    Buffer const& meshletIndexBuffer() const { return *m_meshletIndexBuffer; }
    Buffer& meshletIndexBuffer() { return *m_meshletIndexBuffer; }

    // Max that can be loaded in the GPU at any time
    static constexpr size_t MaxLoadedVertices         = 12'000'000;
    static constexpr size_t MaxLoadedSkinningVertices = 10'000;
    static constexpr size_t MaxLoadedVelocityVertices = 10'000;
    static constexpr size_t MaxLoadedMorphTargetVertices = 10'000;
    static constexpr size_t MaxLoadedHairVertices     = 4'000'000;
    static constexpr size_t MaxLoadedTriangles        = 16'000'000;
    static constexpr size_t MaxLoadedIndices          = 3 * MaxLoadedTriangles;

    // If there were exactly 124 triangles per meshlet it'd be trivial, but not every meshlet will be filled.
    // Here we add a factor of 2 to allow for a worst case where every meshlet is only half-full.
    static constexpr size_t MaxLoadedMeshlets         = MaxLoadedTriangles / 124 * 2;

    u32 numAllocatedIndices() const
    {
        OffsetAllocator::StorageReport report = m_indexAllocator.storageReport();
        return MaxLoadedIndices - report.totalFreeSpace;
    }

    u32 numAllocatedVertices() const
    {
        OffsetAllocator::StorageReport report = m_vertexAllocator.storageReport();
        return MaxLoadedVertices - report.totalFreeSpace;
    }

    u32 numAllocatedSkinningVertices() const
    {
        OffsetAllocator::StorageReport report = m_skinningVertexAllocator.storageReport();
        return MaxLoadedSkinningVertices - report.totalFreeSpace;
    }

    u32 numAllocatedVelocityVertices() const
    {
        OffsetAllocator::StorageReport report = m_velocityVertexAllocator.storageReport();
        return MaxLoadedVelocityVertices - report.totalFreeSpace;
    }

    u32 numAllocatedMorphTargetVertices() const
    {
        OffsetAllocator::StorageReport report = m_morphTargetVertexAllocator.storageReport();
        return MaxLoadedMorphTargetVertices - report.totalFreeSpace;
    }

    u32 numAllocatedHairVertices() const
    {
        OffsetAllocator::StorageReport report = m_hairVertexAllocator.storageReport();
        return MaxLoadedHairVertices - report.totalFreeSpace;
    }

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
    VertexLayout const m_morphTargetVertexLayout { VertexComponent::Position3F,
                                                   VertexComponent::Normal3F,
                                                   VertexComponent::Tangent3F };
    VertexLayout const m_hairPositionVertexLayout { VertexComponent::Position3F };
    VertexLayout const m_hairAttributeVertexLayout { VertexComponent::Color4U8 };

    std::unique_ptr<Buffer> m_indexBuffer { nullptr };
    OffsetAllocator::Allocator m_indexAllocator { MaxLoadedIndices };

    std::unique_ptr<Buffer> m_positionOnlyVertexBuffer {};
    std::unique_ptr<Buffer> m_nonPositionVertexBuffer {};
    OffsetAllocator::Allocator m_vertexAllocator { MaxLoadedVertices };

    std::unique_ptr<Buffer> m_skinningDataVertexBuffer {};
    OffsetAllocator::Allocator m_skinningVertexAllocator { MaxLoadedSkinningVertices };

    std::unique_ptr<Buffer> m_velocityDataVertexBuffer {};
    OffsetAllocator::Allocator m_velocityVertexAllocator { MaxLoadedVelocityVertices };

    std::unique_ptr<Buffer> m_morphTargetVertexBuffer {};
    OffsetAllocator::Allocator m_morphTargetVertexAllocator { MaxLoadedMorphTargetVertices };

    std::unique_ptr<Buffer> m_hairPositionVertexBuffer {};
    std::unique_ptr<Buffer> m_hairAttributeVertexBuffer {};
    OffsetAllocator::Allocator m_hairVertexAllocator { MaxLoadedHairVertices };

    std::unique_ptr<Buffer> m_meshletVertexIndirectionBuffer {};
    u32 m_nextFreeMeshletIndirIndex { 0 };

    std::unique_ptr<Buffer> m_meshletIndexBuffer {};
    u32 m_nextFreeMeshletIndexBufferIndex { 0 };

    std::vector<ShaderMeshlet> m_meshlets {};
    std::unique_ptr<Buffer> m_meshletBuffer {};
    u32 m_nextFreeMeshletIndex { 0 };

    // TODO: Remove me / rewrite for streaming
    void uploadMeshDataForAllocation(MeshSegmentAsset const&, VertexAllocation const&);

    enum class MeshStreamingState {
        PendingAllocation = 0,
        LoadingData, // TODO: Remove this non-streaming variant!
        StreamingVertexData,
        StreamingMorphTargetData,
        StreamingIndexData,
        StreamingMeshletData,
        CreatingBLAS,
        CompactingBLAS,
        Loaded,
    };

    struct StreamingMesh {
        StaticMesh* mesh;
        MeshStreamingState state;

        bool includeIndices { false };
        bool includeSkinningData { false };
        bool includeMorphTargetData { false };
        bool includeVelocityData { false };

        u32 nextLOD { 0 };
        u32 nextSegment { 0 };
        u32 nextMeshlet { 0 };
        u32 nextMorphTarget { 0 };
        u32 nextVertex { 0 };
        u32 nextIndex { 0 };

        void setNextState(MeshStreamingState nextState)
        {
            state = nextState;

            nextLOD = 0;
            nextSegment = 0;
            nextMeshlet = 0;
            nextMorphTarget = 0;
            nextVertex = 0;
            nextIndex = 0;
        }
    };

    struct StreamingSkeletalMesh {
        SkeletalMeshInstance* skeletalMeshInstance;
        std::vector<VertexAllocation::Internal> owningAllocations;
    };

    enum class HairStreamingState {
        PendingAllocation = 0,
        StreamingPositionData,
        StreamingAttributeData,
        StreamingIndexData,
        Loaded,
    };

    struct StreamingHairMesh {
        HairMesh* hairMesh {};
        HairStreamingState state { HairStreamingState::PendingAllocation };

        u32 nextStrand { 0 };
        u32 nextVertex { 0 };

        void setNextState(HairStreamingState nextState)
        {
            state = nextState;

            nextStrand = 0;
            nextVertex = 0;
        }
    };

    template<typename F>
    bool processStreamingMeshState(VertexManager::StreamingMesh& streamingMesh, F&& processSegmentCallback);

    std::vector<StreamingMesh> m_streamingMeshes {};
    std::vector<StreamingSkeletalMesh> m_streamingSkeletalMeshes {};
    std::vector<StreamingHairMesh> m_streamingHairMeshes {};

    VertexAllocation allocateMeshDataForSegment(MeshSegmentAsset const&, bool includeIndices, bool includeSkinningData, bool includeMorphData, bool includeVelocityData);
    bool streamVertexData(StreamingMesh&, StaticMeshSegment const&, UploadBuffer&);
    bool streamMorphTargetData(StreamingMesh&, StaticMeshSegment const&, UploadBuffer&);
    bool streamIndexData(StreamingMesh&, StaticMeshSegment const&, UploadBuffer&);
    std::optional<MeshletView> streamMeshletDataForSegment(StreamingMesh& streamingMesh, StaticMeshSegment const&, UploadBuffer&);
    std::unique_ptr<BottomLevelAS> createBottomLevelAccelerationStructure(VertexAllocation const&);
};
