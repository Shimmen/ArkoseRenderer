#include "VertexManager.h"

#include "asset/MeshAsset.h"
#include "rendering/StaticMesh.h"
#include "rendering/backend/base/Backend.h"
#include "scene/MeshInstance.h"

VertexManager::VertexManager(Backend& backend)
{
    const size_t initialIndexBufferSize = 100'000 * sizeofIndexType(indexType());
    const size_t initialPostionVertexBufferSize = 50'000 * positionVertexLayout().packedVertexSize();
    const size_t initialNonPostionVertexBufferSize = 50'000 * nonPositionVertexLayout().packedVertexSize();
    constexpr Buffer::MemoryHint memoryHint = Buffer::MemoryHint::GpuOptimal;

    m_indexBuffer = backend.createBuffer(initialIndexBufferSize, Buffer::Usage::Index, memoryHint);
    m_indexBuffer->setName("SceneIndexBuffer");

    m_positionOnlyVertexBuffer = backend.createBuffer(initialPostionVertexBufferSize, Buffer::Usage::Vertex, memoryHint);
    m_positionOnlyVertexBuffer->setName("ScenePositionOnlyVertexBuffer");

    m_nonPositionVertexBuffer = backend.createBuffer(initialNonPostionVertexBufferSize, Buffer::Usage::Vertex, memoryHint);
    m_nonPositionVertexBuffer->setName("SceneNonPositionVertexBuffer");
}

VertexManager::~VertexManager()
{
}

bool VertexManager::allocateMeshData(StaticMesh& staticMesh)
{
    SCOPED_PROFILE_ZONE();

    for (StaticMeshLOD& lod : staticMesh.LODs()) {
        for (StaticMeshSegment& meshSegment : lod.meshSegments) {
            if (!allocateMeshDataForSegment(meshSegment)) {
                return false;
            }
        }
    }

    return true;
}

bool VertexManager::uploadMeshData(StaticMesh& staticMesh)
{
    SCOPED_PROFILE_ZONE();

    for (StaticMeshLOD& lod : staticMesh.LODs()) {
        for (StaticMeshSegment& meshSegment : lod.meshSegments) {
            
            if (!allocateMeshDataForSegment(meshSegment)) {
                return false;
            }

            // TODO: Implement async uploading, i.e., push this vertex upload job to a queue!
            uploadMeshDataForAllocation(VertexUploadJob { .asset = meshSegment.asset,
                                                          .allocation = meshSegment.vertexAllocation });
        }
    }

    return true;
}

bool VertexManager::allocateMeshDataForSegment(StaticMeshSegment& meshSegment)
{
    SCOPED_PROFILE_ZONE();

    if (meshSegment.vertexAllocation.isValid()) {
        return true;
    }

    MeshSegmentAsset const& segmentAsset = *meshSegment.asset;
    u32 vertexCount = narrow_cast<u32>(segmentAsset.vertexCount());
    u32 indexCount = narrow_cast<u32>(segmentAsset.indices.size());

    // TODO: Validate that it will fit!
    meshSegment.vertexAllocation.firstVertex = m_nextFreeVertexIndex;
    meshSegment.vertexAllocation.vertexCount = vertexCount;
    m_nextFreeVertexIndex += vertexCount;

    if (indexCount > 0) {
        // TODO: Validate that it will fit!
        meshSegment.vertexAllocation.firstIndex = m_nextFreeIndex;
        meshSegment.vertexAllocation.indexCount = indexCount;
        m_nextFreeIndex += indexCount;
    }

    return true;
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
    //if (segmentAsset.jointIndices.size() > 0 || segmentAsset.jointWeights.size() > 0) {
    //    ARKOSE_ASSERT(segmentAsset.jointIndices.size() == segmentAsset.jointWeights.size());
    //    NOT_YET_IMPLEMENTED();
    //}

    // Upload index data if relevant
    if (allocation.indexCount > 0) {
        size_t indexOffset = allocation.firstIndex * sizeofIndexType(indexType());
        m_indexBuffer->updateDataAndGrowIfRequired(segmentAsset.indices.data(), segmentAsset.indices.size(), indexOffset);
    }

    // TODO: We need to communicate somehow once the upload is done and we can start rendering with it!
    //allocation.uploaded = true;
}

void VertexManager::skinSkeletalMeshInstance(SkeletalMeshInstance& instance)
{
    std::vector<mat4> const& jointMatrices = instance.skeleton().appliedJointMatrices();
    std::vector<mat3> const& jointTangentMatrices = instance.skeleton().appliedJointTangentMatrices();

    // TODO: Issue compute shader which applies the joint matrices to the instance's vertices
    // NOT_YET_IMPLEMENTED();
}
