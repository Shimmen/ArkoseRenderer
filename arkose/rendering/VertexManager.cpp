#include "VertexManager.h"

#include "asset/MeshAsset.h"
#include "rendering/StaticMesh.h"
#include "rendering/backend/base/AccelerationStructure.h"
#include "rendering/backend/base/Backend.h"
#include "rendering/backend/base/Buffer.h"
#include "scene/MeshInstance.h"
#include <ark/conversion.h>

VertexManager::VertexManager(Backend& backend)
    : m_backend(&backend)
{
    const size_t indexBufferSize = MaxLoadedIndices * sizeofIndexType(indexType());
    const size_t postionVertexBufferSize = MaxLoadedVertices * positionVertexLayout().packedVertexSize();
    const size_t nonPostionVertexBufferSize = MaxLoadedVertices * nonPositionVertexLayout().packedVertexSize();
    const size_t skinningDataVertexBufferSize = MaxLoadedSkinningVertices * skinningDataVertexLayout().packedVertexSize();
    const size_t velocityDataVertexBufferSize = MaxLoadedVelocityVertices * velocityDataVertexLayout().packedVertexSize();

    float totalMemoryUseMb = ark::conversion::to::MB(indexBufferSize
                                                     + postionVertexBufferSize
                                                     + nonPostionVertexBufferSize
                                                     + skinningDataVertexBufferSize
                                                     + velocityDataVertexBufferSize);
    ARKOSE_LOG(Info, "VertexManager: allocating a total of {:.1f} MB of VRAM for vertex data", totalMemoryUseMb);

    m_indexBuffer = backend.createBuffer(indexBufferSize, Buffer::Usage::Index);
    m_indexBuffer->setStride(sizeofIndexType(indexType()));
    m_indexBuffer->setName("SceneIndexBuffer");

    m_positionOnlyVertexBuffer = backend.createBuffer(postionVertexBufferSize, Buffer::Usage::Vertex);
    m_positionOnlyVertexBuffer->setStride(positionVertexLayout().packedVertexSize());
    m_positionOnlyVertexBuffer->setName("ScenePositionOnlyVertexBuffer");

    m_nonPositionVertexBuffer = backend.createBuffer(nonPostionVertexBufferSize, Buffer::Usage::Vertex);
    m_nonPositionVertexBuffer->setStride(nonPositionVertexLayout().packedVertexSize());
    m_nonPositionVertexBuffer->setName("SceneNonPositionVertexBuffer");

    m_skinningDataVertexBuffer = backend.createBuffer(skinningDataVertexBufferSize, Buffer::Usage::Vertex);
    m_skinningDataVertexBuffer->setStride(skinningDataVertexLayout().packedVertexSize());
    m_skinningDataVertexBuffer->setName("SceneSkinningDataVertexBuffer");

    m_velocityDataVertexBuffer = backend.createBuffer(velocityDataVertexBufferSize, Buffer::Usage::Vertex);
    m_velocityDataVertexBuffer->setStride(velocityDataVertexLayout().packedVertexSize());
    m_velocityDataVertexBuffer->setName("SceneVelocityDataVertexBuffer");
}

VertexManager::~VertexManager()
{
}

bool VertexManager::uploadMeshData(StaticMesh& staticMesh, bool includeIndices, bool includeSkinningData)
{
    SCOPED_PROFILE_ZONE();

    for (StaticMeshLOD& lod : staticMesh.LODs()) {
        for (StaticMeshSegment& meshSegment : lod.meshSegments) {

            if (meshSegment.vertexAllocation.isValid()) {
                continue;
            }

            // There are (currently) no cases where we have velocity data from an asset that we need to upload
            constexpr bool includeVelocityData = false;

            VertexAllocation allocation = allocateMeshDataForSegment(*meshSegment.asset, includeIndices, includeSkinningData, includeVelocityData);
            if (!allocation.isValid()) {
                return false;
            }

            // TODO: Implement async uploading, i.e., push this vertex upload job to a queue!
            uploadMeshDataForAllocation(VertexUploadJob { .asset = meshSegment.asset,
                                                          .target = &meshSegment,
                                                          .allocation = allocation });
        }
    }

    return true;
}

bool VertexManager::createBottomLevelAccelerationStructure(StaticMesh& staticMesh)
{
    SCOPED_PROFILE_ZONE();

    for (StaticMeshLOD& lod : staticMesh.LODs()) {
        for (StaticMeshSegment& meshSegment : lod.meshSegments) {
            ARKOSE_ASSERT(meshSegment.vertexAllocation.isValid());
            meshSegment.blas = createBottomLevelAccelerationStructure(meshSegment.vertexAllocation, nullptr);
        }
    }

    return true;
}

VertexAllocation VertexManager::allocateMeshDataForSegment(MeshSegmentAsset const& segmentAsset, bool includeIndices, bool includeSkinningData, bool includeVelocityData)
{
    SCOPED_PROFILE_ZONE();

    u32 vertexCount = narrow_cast<u32>(segmentAsset.vertexCount());
    u32 indexCount = narrow_cast<u32>(segmentAsset.indices.size());

    VertexAllocation allocation {};

    // TODO: Validate that it will fit!
    allocation.firstVertex = m_nextFreeVertexIndex;
    allocation.vertexCount = vertexCount;
    m_nextFreeVertexIndex += vertexCount;

    if (indexCount > 0 && includeIndices) {
        // TODO: Validate that it will fit!
        allocation.firstIndex = m_nextFreeIndex;
        allocation.indexCount = indexCount;
        m_nextFreeIndex += indexCount;
    }

    if (segmentAsset.hasSkinningData() && includeSkinningData) {
        // TODO: Validate that it will fit!
        allocation.firstSkinningVertex = m_nextFreeSkinningVertexIndex;
        m_nextFreeSkinningVertexIndex += vertexCount;
    }

    if (includeVelocityData) {
        // TODO: Validate that it will fit!
        allocation.firstVelocityVertex = m_nextFreeVelocityIndex;
        m_nextFreeVelocityIndex += vertexCount;
    }

    ARKOSE_ASSERT(allocation.isValid());
    return allocation;
}

void VertexManager::uploadMeshDataForAllocation(VertexUploadJob const& uploadJob)
{
    SCOPED_PROFILE_ZONE();

    MeshSegmentAsset const& segmentAsset = *uploadJob.asset;
    VertexAllocation const& allocation = uploadJob.allocation;

    ARKOSE_ASSERT(allocation.vertexCount > 0);
    ARKOSE_ASSERT(allocation.vertexCount == segmentAsset.vertexCount());
    ARKOSE_ASSERT(allocation.indexCount == segmentAsset.indices.size());

    // TODO: Upload on the command list to avoid stalling the GPU!

    // Upload position-only vertex data
    {
        std::vector<u8> positionOnlyVertexData = segmentAsset.assembleVertexData(m_positionOnlyVertexLayout);
        size_t positionOnlyVertexOffset = allocation.firstVertex * m_positionOnlyVertexLayout.packedVertexSize();
        m_positionOnlyVertexBuffer->updateData(positionOnlyVertexData.data(), positionOnlyVertexData.size(), positionOnlyVertexOffset);
    }

    // Upload non-position vertex data
    {
        std::vector<u8> nonPositionVertexData = segmentAsset.assembleVertexData(m_nonPositionVertexLayout);
        size_t nonPositionVertexOffset = allocation.firstVertex * m_nonPositionVertexLayout.packedVertexSize();
        m_nonPositionVertexBuffer->updateData(nonPositionVertexData.data(), nonPositionVertexData.size(), nonPositionVertexOffset);
    }

    // Upload skinning data if relevant
    if (allocation.firstSkinningVertex >= 0) {
        ARKOSE_ASSERT(segmentAsset.hasSkinningData());
        ARKOSE_ASSERT(segmentAsset.jointIndices.size() == segmentAsset.jointWeights.size());
        std::vector<u8> skinningVertexData = segmentAsset.assembleVertexData(m_skinningDataVertexLayout);
        size_t skinningDataOffset = allocation.firstSkinningVertex * m_skinningDataVertexLayout.packedVertexSize();
        m_skinningDataVertexBuffer->updateData(skinningVertexData.data(), skinningVertexData.size(), skinningDataOffset);
    }

    // Upload index data if relevant
    if (allocation.indexCount > 0) {
        size_t indexSize = sizeofIndexType(indexType());
        size_t indexOffset = allocation.firstIndex * indexSize;
        m_indexBuffer->updateData(segmentAsset.indices.data(), segmentAsset.indices.size() * indexSize, indexOffset);
    }

    // The data is now uploaded, indicate that it's ready to be used
    // TODO: When we're async this will be a race condition! We might wanna do like the MeshletManager for signalling back.
    uploadJob.target->vertexAllocation = allocation;
}

std::unique_ptr<BottomLevelAS> VertexManager::createBottomLevelAccelerationStructure(VertexAllocation const& vertexAllocation, BottomLevelAS const* copySource)
{
    // TODO: Create a geometry per mesh (or rather, per LOD) and use the SBT to lookup material.
    // For now we create one per segment so we can ensure one material per "draw"

    ARKOSE_ASSERT(positionVertexLayout().components().at(0) == VertexComponent::Position3F);
    constexpr RTVertexFormat vertexFormat = RTVertexFormat::XYZ32F;

    size_t vertexStride = positionVertexLayout().packedVertexSize();

    DrawCallDescription drawCallDesc = DrawCallDescription::fromVertexAllocation(vertexAllocation);
    ARKOSE_ASSERT(drawCallDesc.type == DrawCallDescription::Type ::Indexed);

    i32 indexOfFirstVertex = drawCallDesc.vertexOffset;
    size_t vertexOffset = indexOfFirstVertex * vertexStride;

    u32 indexOfFirstIndex = drawCallDesc.firstIndex;
    size_t indexOffset = indexOfFirstIndex * sizeofIndexType(indexType());

    RTTriangleGeometry geometry { .vertexBuffer = positionVertexBuffer(),
                                  .vertexCount = drawCallDesc.vertexCount,
                                  .vertexOffset = vertexOffset,
                                  .vertexStride = vertexStride,
                                  .vertexFormat = vertexFormat,
                                  .indexBuffer = indexBuffer(),
                                  .indexCount = drawCallDesc.indexCount,
                                  .indexOffset = indexOffset,
                                  .indexType = indexType(),
                                  .transform = mat4(1.0f) };

    return m_backend->createBottomLevelAccelerationStructure({ geometry }, copySource);
}
