#include "MeshletManager.h"

#include "asset/StaticMeshAsset.h"
#include "core/Conversion.h"
#include "core/Logging.h"
#include "rendering/StaticMesh.h"
#include "rendering/backend/base/Backend.h"
#include "rendering/backend/base/CommandList.h"
#include "rendering/backend/util/UploadBuffer.h"

MeshletManager::MeshletManager(Backend& backend)
{
    size_t positionDataBufferSize = m_positionVertexLayout.packedVertexSize() * MaxLoadedVertices;
    size_t nonPositionDataBufferSize = m_nonPositionVertexLayout.packedVertexSize() * MaxLoadedVertices;
    size_t loadedIndexBufferSize = sizeof(u32) * MaxLoadedIndices;
    size_t meshletBufferSize = sizeof(ShaderMeshlet) * MaxLoadedMeshlets;

    float totalMemoryUseMb = conversion::to::MB(positionDataBufferSize + nonPositionDataBufferSize + loadedIndexBufferSize + meshletBufferSize);
    ARKOSE_LOG(Info, "MeshletManager: allocating a total of {:.1f} MB of VRAM for meshlet vertex and index data", totalMemoryUseMb);

    m_positionDataVertexBuffer = backend.createBuffer(positionDataBufferSize, Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOnly);
    m_positionDataVertexBuffer->setName("MeshletPositionVertexData");

    m_nonPositionDataVertexBuffer = backend.createBuffer(nonPositionDataBufferSize, Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOnly);
    m_nonPositionDataVertexBuffer->setName("MeshletNonPositionVertexData");

    m_indexBuffer = backend.createBuffer(loadedIndexBufferSize, Buffer::Usage::Index, Buffer::MemoryHint::GpuOnly);
    m_indexBuffer->setName("MeshletIndexData");

    m_meshletBuffer = backend.createBuffer(meshletBufferSize, Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOnly);
    m_meshletBuffer->setName("MeshletData");

    m_uploadBuffer = std::make_unique<UploadBuffer>(backend, UploadBufferSize);
}

MeshletManager::~MeshletManager() = default;

void MeshletManager::allocateMeshlets(StaticMesh& staticMesh)
{
    for (StaticMeshLOD& lod : staticMesh.LODs()) {
        for (StaticMeshSegment& meshSegment : lod.meshSegments) {

            // TODO: Do this at asset processing time, not in runtime!
            const_cast<StaticMeshSegmentAsset*>(meshSegment.asset)->generateMeshlets();

            m_segmentsAwaitingUpload.push_back(&meshSegment);
        }
    }
}

void MeshletManager::freeMeshlets(StaticMesh&)
{
    // TODO: Free memory from the buffers! Implement a proper allocator to support this..
}

void MeshletManager::processMeshStreaming(CommandList& cmdList)
{
    SCOPED_PROFILE_ZONE();

    m_uploadBuffer->reset();

    u32 numProcessedSegments = 0;
    for (StaticMeshSegment* meshSegment : m_segmentsAwaitingUpload) {

        SCOPED_PROFILE_ZONE_NAMED("Processing segment");

        StaticMeshSegmentAsset const& meshSegmentAsset = *meshSegment->asset;
        MeshletDataAsset const& meshletDataAsset = meshSegmentAsset.meshletData.value();

        u32 vertexCount = narrow_cast<u32>(meshletDataAsset.meshletVertexPositions.size());
        u32 indexCount = narrow_cast<u32>(meshletDataAsset.meshletIndices.size());
        u32 meshletCount = narrow_cast<u32>(meshletDataAsset.meshlets.size());

        size_t totalUploadSize = vertexCount * (sizeof(vec3) /* + sizeof(remaining vertex data)*/) + indexCount * sizeof(u32) + meshletCount * sizeof(ShaderMeshlet);
        if (totalUploadSize > m_uploadBuffer->remainingSize()) {
            if (totalUploadSize > UploadBufferSize) {
                ARKOSE_LOG(Fatal, "Static mesh segment is {:.2f} MB but the meshlet upload budget is only {:.2f} MB. "
                                  "The budget must be increased if we want to be able to load this asset.",
                           conversion::to::MB(totalUploadSize), conversion::to::MB(UploadBufferSize));
            }
            break;
        }

        size_t posDataOffset = m_nextVertexIdx * m_positionVertexLayout.packedVertexSize();
        m_uploadBuffer->upload(meshletDataAsset.meshletVertexPositions, *m_positionDataVertexBuffer, posDataOffset);

        // Offset indices by current vertex count as we put all meshlets in a single buffer
        std::vector<u32> adjustedMeshletIndices = meshletDataAsset.meshletIndices;
        for (u32& index : adjustedMeshletIndices) {
            index += m_nextVertexIdx;
        }

        size_t indexDataOffset = m_nextIndexIdx * sizeof(u32);
        m_uploadBuffer->upload(adjustedMeshletIndices, *m_indexBuffer, indexDataOffset);

        for (MeshletAsset const& meshletAsset : meshletDataAsset.meshlets) {

            ShaderMeshlet meshlet { .firstIndex = m_nextIndexIdx + meshletAsset.firstIndex,
                                    .triangleCount = meshletAsset.triangleCount,
                                    .materialIndex = meshSegment->material.indexOfType<uint>(),
                                    .transformIndex = 0, // TODO!
                                    .center = meshletAsset.center,
                                    .radius = meshletAsset.radius };

            m_meshlets.push_back(meshlet);
        }

        size_t meshletDataDstOffset = m_nextMeshletIdx * sizeof(ShaderMeshlet);
        m_uploadBuffer->upload(m_meshlets.data() + m_nextMeshletIdx, meshletCount * sizeof(ShaderMeshlet), *m_meshletBuffer, meshletDataDstOffset);

        // Setup the meshlet view for this segment
        meshSegment->meshletView = { .firstMeshlet = m_nextMeshletIdx,
                                     .meshletCount = meshletCount };

        numProcessedSegments += 1;
        m_nextVertexIdx += vertexCount;
        m_nextIndexIdx += indexCount;
        m_nextMeshletIdx += meshletCount;
    }

    if (numProcessedSegments > 0) {
        ARKOSE_ASSERT(!m_uploadBuffer->peekPendingOperations().empty());
        cmdList.executeBufferCopyOperations(*m_uploadBuffer);

        auto start = m_segmentsAwaitingUpload.begin();
        m_segmentsAwaitingUpload.erase(start, start + numProcessedSegments);

        //ARKOSE_LOG(Verbose, "Uploading {} segments, {} remaining", numProcessedSegments, m_segmentsAwaitingUpload.size());
    }
}