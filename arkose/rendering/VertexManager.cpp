#include "VertexManager.h"

#include "asset/MeshAsset.h"
#include "rendering/StaticMesh.h"
#include "rendering/backend/base/AccelerationStructure.h"
#include "rendering/backend/base/Backend.h"
#include "rendering/backend/base/Buffer.h"
#include "scene/MeshInstance.h"

DrawCallDescription VertexAllocation::asDrawCallDescription() const
{
    DrawCallDescription drawCall {};

    if (indexCount > 0) {
        drawCall.type = DrawCallDescription::Type::Indexed;
        drawCall.vertexOffset = firstVertex;
        drawCall.vertexCount = vertexCount;
        drawCall.firstIndex = firstIndex;
        drawCall.indexCount = indexCount;
    } else {
        drawCall.type = DrawCallDescription::Type::NonIndexed;
        drawCall.firstVertex = firstVertex;
        drawCall.vertexCount = vertexCount;
    }

    return drawCall;
}

VertexManager::VertexManager(Backend& backend)
    : m_backend(&backend)
{
    const size_t initialIndexBufferSize = 100'000 * sizeofIndexType(indexType());
    const size_t initialPostionVertexBufferSize = 50'000 * positionVertexLayout().packedVertexSize();
    const size_t initialNonPostionVertexBufferSize = 50'000 * nonPositionVertexLayout().packedVertexSize();
    const size_t initialSkinningDataVertexBufferSize = 10'000 * skinningDataVertexLayout().packedVertexSize();
    const size_t initialVelocityDataVertexBufferSize = 10'000 * velocityDataVertexLayout().packedVertexSize();

    m_indexBuffer = backend.createBuffer(initialIndexBufferSize, Buffer::Usage::Index);
    m_indexBuffer->setName("SceneIndexBuffer");

    m_positionOnlyVertexBuffer = backend.createBuffer(initialPostionVertexBufferSize, Buffer::Usage::Vertex);
    m_positionOnlyVertexBuffer->setName("ScenePositionOnlyVertexBuffer");

    m_nonPositionVertexBuffer = backend.createBuffer(initialNonPostionVertexBufferSize, Buffer::Usage::Vertex);
    m_nonPositionVertexBuffer->setName("SceneNonPositionVertexBuffer");

    m_skinningDataVertexBuffer = backend.createBuffer(initialSkinningDataVertexBufferSize, Buffer::Usage::Vertex);
    m_skinningDataVertexBuffer->setName("SceneSkinningDataVertexBuffer");

    m_velocityDataVertexBuffer = backend.createBuffer(initialVelocityDataVertexBufferSize, Buffer::Usage::Vertex);
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
        m_positionOnlyVertexBuffer->updateDataAndGrowIfRequired(positionOnlyVertexData.data(), positionOnlyVertexData.size(), positionOnlyVertexOffset);
    }

    // Upload non-position vertex data
    {
        std::vector<u8> nonPositionVertexData = segmentAsset.assembleVertexData(m_nonPositionVertexLayout);
        size_t nonPositionVertexOffset = allocation.firstVertex * m_nonPositionVertexLayout.packedVertexSize();
        m_nonPositionVertexBuffer->updateDataAndGrowIfRequired(nonPositionVertexData.data(), nonPositionVertexData.size(), nonPositionVertexOffset);
    }

    // Upload skinning data if relevant
    if (allocation.firstSkinningVertex >= 0) {
        ARKOSE_ASSERT(segmentAsset.hasSkinningData());
        ARKOSE_ASSERT(segmentAsset.jointIndices.size() == segmentAsset.jointWeights.size());
        std::vector<u8> skinningVertexData = segmentAsset.assembleVertexData(m_skinningDataVertexLayout);
        size_t skinningDataOffset = allocation.firstSkinningVertex * m_skinningDataVertexLayout.packedVertexSize();
        m_skinningDataVertexBuffer->updateDataAndGrowIfRequired(skinningVertexData.data(), skinningVertexData.size(), skinningDataOffset);
    }

    // Upload index data if relevant
    if (allocation.indexCount > 0) {
        size_t indexSize = sizeofIndexType(indexType());
        size_t indexOffset = allocation.firstIndex * indexSize;
        m_indexBuffer->updateDataAndGrowIfRequired(segmentAsset.indices.data(), segmentAsset.indices.size() * indexSize, indexOffset);
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

    DrawCallDescription drawCallDesc = vertexAllocation.asDrawCallDescription();
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
