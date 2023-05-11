#include "MeshletManager.h"

#include "asset/StaticMeshAsset.h"
#include "core/Conversion.h"
#include "core/Logging.h"
#include "rendering/StaticMesh.h"
#include "rendering/backend/base/Backend.h"
#include "rendering/backend/base/CommandList.h"
#include "rendering/backend/util/UploadBuffer.h"

// Shader headers
#include "shaders/shared/MeshletVertex.h"

MeshletManager::MeshletManager(Backend& backend)
{
    ARKOSE_ASSERT(m_nonPositionVertexLayout.packedVertexSize() == sizeof(MeshletNonPositionVertex));
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

void MeshletManager::processMeshStreaming(CommandList& cmdList, std::unordered_set<StaticMeshHandle>& updatedMeshes)
{
    SCOPED_PROFILE_ZONE();

    m_uploadBuffer->reset();

    u32 numProcessedSegments = 0;
    for (StaticMeshSegment* meshSegment : m_segmentsAwaitingUpload) {

        SCOPED_PROFILE_ZONE_NAMED("Processing segment");

        StaticMeshSegmentAsset const& meshSegmentAsset = *meshSegment->asset;
        MeshletDataAsset const& meshletDataAsset = meshSegmentAsset.meshletData.value();

        u32 vertexCount = narrow_cast<u32>(meshletDataAsset.meshletVertexIndirection.size());
        u32 indexCount = narrow_cast<u32>(meshletDataAsset.meshletIndices.size());
        u32 meshletCount = narrow_cast<u32>(meshletDataAsset.meshlets.size());

        size_t totalUploadSize =
            vertexCount * (m_positionVertexLayout.packedVertexSize() + m_nonPositionVertexLayout.packedVertexSize())
            + indexCount * sizeof(u32)
            + meshletCount * sizeof(ShaderMeshlet);

        // TODO: There are instances where segments are massive, so we need to allow uploading with a finer granularity.
        if (totalUploadSize > m_uploadBuffer->remainingSize()) {
            if (totalUploadSize > UploadBufferSize) {
                ARKOSE_LOG(Fatal, "Static mesh segment is {:.2f} MB but the meshlet upload budget is only {:.2f} MB. "
                                  "The budget must be increased if we want to be able to load this asset.",
                           conversion::to::MB(totalUploadSize), conversion::to::MB(UploadBufferSize));
            }
            break;
        }

        // Offset indices by current vertex count as we put all meshlets in a single buffer
        std::vector<u32> adjustedMeshletIndices = meshletDataAsset.meshletIndices;
        for (u32& index : adjustedMeshletIndices) {
            index += m_nextVertexIdx;
        }

        size_t indexDataOffset = m_nextIndexIdx * sizeof(u32);
        m_uploadBuffer->upload(adjustedMeshletIndices, *m_indexBuffer, indexDataOffset);

        u32 startVertexIdx = m_nextVertexIdx;

        std::vector<vec3> positionsTempVector {};
        positionsTempVector.reserve(vertexCount);

        std::vector<MeshletNonPositionVertex> nonPositionsTempVector {};
        nonPositionsTempVector.reserve(vertexCount);

        for (MeshletAsset const& meshletAsset : meshletDataAsset.meshlets) {

            ShaderMeshlet meshlet { .firstIndex = m_nextIndexIdx + meshletAsset.firstIndex,
                                    .triangleCount = meshletAsset.triangleCount,
                                    .firstVertex = m_nextVertexIdx,
                                    .vertexCount = meshletAsset.vertexCount,
                                    .center = meshletAsset.center,
                                    .radius = meshletAsset.radius };

            m_meshlets.push_back(meshlet);

            // Remap vertices
            for (u32 i = 0; i < meshletAsset.vertexCount; ++i) {
                u32 vertexIdx = meshletDataAsset.meshletVertexIndirection[meshletAsset.firstVertex + i];

                vec3 position = meshSegment->asset->positions[vertexIdx];
                vec2 texcoord0 = (vertexIdx < meshSegment->asset->texcoord0s.size()) ? meshSegment->asset->texcoord0s[vertexIdx] : vec2(0.0f, 0.0f);
                vec3 normal = (vertexIdx < meshSegment->asset->normals.size()) ? meshSegment->asset->normals[vertexIdx] : vec3(0.0f, 0.0f, 1.0f);
                vec4 tangent = (vertexIdx < meshSegment->asset->tangents.size()) ? meshSegment->asset->tangents[vertexIdx] : vec4(1.0f, 0.0f, 0.0f, 1.0f);

                positionsTempVector.emplace_back(position);
                nonPositionsTempVector.emplace_back(MeshletNonPositionVertex { .texcoord0 = texcoord0,
                                                                               .normal = normal,
                                                                               .tangent = tangent });
            }

            m_nextVertexIdx += meshletAsset.vertexCount;
        }

        // TODO: This MAY is still too many buffer uploads.. we need to be more efficient.
        // Additionally, keep in mind that some of these buffer copies are contiguous..?
        size_t posDataOffset = startVertexIdx * m_positionVertexLayout.packedVertexSize();
        m_uploadBuffer->upload(positionsTempVector, *m_positionDataVertexBuffer, posDataOffset);

        size_t nonPosDataOffset = startVertexIdx * m_nonPositionVertexLayout.packedVertexSize();
        m_uploadBuffer->upload(nonPositionsTempVector, *m_nonPositionDataVertexBuffer, nonPosDataOffset);

        size_t meshletDataDstOffset = m_nextMeshletIdx * sizeof(ShaderMeshlet);
        m_uploadBuffer->upload(m_meshlets.data() + m_nextMeshletIdx, meshletCount * sizeof(ShaderMeshlet), *m_meshletBuffer, meshletDataDstOffset);

        // Setup the meshlet view for this segment
        meshSegment->meshletView = { .firstMeshlet = m_nextMeshletIdx,
                                     .meshletCount = meshletCount };

        // Signal to the caller that the mesh has changed
        updatedMeshes.insert(meshSegment->staticMeshHandle);

        numProcessedSegments += 1;
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
