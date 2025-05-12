#include "MeshletManager.h"

#include "asset/MeshAsset.h"
#include "core/Logging.h"
#include "rendering/StaticMesh.h"
#include "rendering/backend/base/Backend.h"
#include "rendering/backend/base/CommandList.h"
#include "rendering/backend/util/UploadBuffer.h"
#include <ark/conversion.h>

// Shader headers
#include "shaders/shared/SceneData.h"

MeshletManager::MeshletManager(Backend& backend)
{
    size_t meshletBufferSize = sizeof(ShaderMeshlet) * MaxLoadedMeshlets;
    size_t meshletIndexBufferSize = sizeof(u32) * MaxLoadedIndices;
    size_t vertexIndirectionBufferSize = sizeof(u32) * MaxLoadedVertices;

    float totalMemoryUseMb = ark::conversion::to::MB(vertexIndirectionBufferSize + meshletIndexBufferSize + meshletBufferSize);
    ARKOSE_LOG(Info, "MeshletManager: allocating a total of {:.1f} MB of VRAM for meshlet data", totalMemoryUseMb);

    m_vertexIndirectionBuffer = backend.createBuffer(vertexIndirectionBufferSize, Buffer::Usage::StorageBuffer);
    m_vertexIndirectionBuffer->setStride(sizeof(u32));
    m_vertexIndirectionBuffer->setName("MeshletVertexIndirectionData");

    m_indexBuffer = backend.createBuffer(meshletIndexBufferSize, Buffer::Usage::Index);
    m_indexBuffer->setStride(sizeof(u32));
    m_indexBuffer->setName("MeshletIndexData");

    m_meshletBuffer = backend.createBuffer(meshletBufferSize, Buffer::Usage::StorageBuffer);
    m_meshletBuffer->setStride(sizeof(ShaderMeshlet));
    m_meshletBuffer->setName("MeshletData");

    m_uploadBuffer = std::make_unique<UploadBuffer>(backend, UploadBufferSize);
}

MeshletManager::~MeshletManager() = default;

void MeshletManager::allocateMeshlets(StaticMesh& staticMesh)
{
    for (StaticMeshLOD& lod : staticMesh.LODs()) {
        for (StaticMeshSegment& meshSegment : lod.meshSegments) {
            if (meshSegment.asset->meshletData.has_value()) {
                m_segmentsAwaitingUpload.push_back(&meshSegment);
            } else {
                ARKOSE_LOG(Warning, "Meshlet manager: skipping mesh segment due to no meshlet data.");
                //const_cast<MeshSegmentAsset*>(meshSegment.asset)->generateMeshlets();
            }
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

        MeshSegmentAsset const& meshSegmentAsset = *meshSegment->asset;
        MeshletDataAsset const& meshletDataAsset = meshSegmentAsset.meshletData.value();

        u32 vertexCount = narrow_cast<u32>(meshletDataAsset.meshletVertexIndirection.size());
        u32 indexCount = narrow_cast<u32>(meshletDataAsset.meshletIndices.size());
        u32 meshletCount = narrow_cast<u32>(meshletDataAsset.meshlets.size());

        size_t totalUploadSize =
            vertexCount * sizeof(u32) // vertex indirection buffer
            + indexCount * sizeof(u32)
            + meshletCount * sizeof(ShaderMeshlet);

        // TODO: There are instances where segments are massive, so we need to allow uploading with a finer granularity.
        if (totalUploadSize > m_uploadBuffer->remainingSize()) {
            if (totalUploadSize > UploadBufferSize) {
                ARKOSE_LOG(Fatal, "Static mesh segment is {:.2f} MB but the meshlet upload budget is only {:.2f} MB. "
                                  "The budget must be increased if we want to be able to load this asset.",
                           ark::conversion::to::MB(totalUploadSize), ark::conversion::to::MB(UploadBufferSize));
            }
            break;
        }

        // Offset indices by current vertex count as we put all meshlets in a single buffer
        std::vector<u32> adjustedMeshletIndices = meshletDataAsset.meshletIndices;
        for (u32& index : adjustedMeshletIndices) {
            index += m_nextVertexIndirectionIdx;
        }

        size_t indexDataOffset = m_nextIndexIdx * sizeof(u32);
        m_uploadBuffer->upload(adjustedMeshletIndices, *m_indexBuffer, indexDataOffset);

        // Offset vertex indirection by the segment's first vertex as we're referencing the global vertex buffers
        std::vector<u32> adjustedVertexIndirection = meshletDataAsset.meshletVertexIndirection;
        for (u32& vertexIndex : adjustedVertexIndirection) {
            vertexIndex += meshSegment->vertexAllocation.firstVertex;
        }

        size_t vertexIndirectionOffset = m_nextVertexIndirectionIdx * sizeof(u32);
        m_uploadBuffer->upload(adjustedVertexIndirection, *m_vertexIndirectionBuffer, vertexIndirectionOffset);

        for (MeshletAsset const& meshletAsset : meshletDataAsset.meshlets) {

            ShaderMeshlet meshlet { .firstIndex = m_nextIndexIdx + meshletAsset.firstIndex,
                                    .triangleCount = meshletAsset.triangleCount,
                                    .firstVertex = m_nextVertexIndirectionIdx,
                                    .vertexCount = meshletAsset.vertexCount,
                                    .center = meshletAsset.center,
                                    .radius = meshletAsset.radius };

            m_meshlets.push_back(meshlet);
            m_nextVertexIndirectionIdx += meshletAsset.vertexCount;
        }

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
