#include "VertexManager.h"

#include "asset/MeshAsset.h"
#include "rendering/GpuScene.h"
#include "rendering/StaticMesh.h"
#include "rendering/SkeletalMesh.h"
#include "rendering/backend/base/AccelerationStructure.h"
#include "rendering/backend/base/Backend.h"
#include "rendering/backend/base/Buffer.h"
#include "rendering/backend/base/CommandList.h"
#include "rendering/backend/util/UploadBuffer.h"
#include "scene/MeshInstance.h"
#include <ark/conversion.h>

VertexManager::VertexManager(Backend& backend, GpuScene& scene)
    : m_backend(&backend)
    , m_scene(&scene)
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

    m_uploadBuffer = std::make_unique<UploadBuffer>(backend, UploadBufferSize);
}

VertexManager::~VertexManager()
{
}

void VertexManager::registerForStreaming(StaticMesh& mesh, bool includeIndices, bool includeSkinningData)
{
    // There are (currently) no cases where we have velocity data from an asset that we need to upload
    constexpr bool includeVelocityData = false;

    m_activeStreamingMeshes.push_back(StreamingMesh { .mesh = &mesh,
                                                      .state = MeshStreamingState::PendingAllocation,
                                                      .includeIndices = includeIndices,
                                                      .includeSkinningData = includeSkinningData,
                                                      .includeVelocityData = includeVelocityData });
}

void VertexManager::processMeshStreaming(CommandList& cmdList, std::unordered_set<StaticMeshHandle>& updatedMeshes)
{
    SCOPED_PROFILE_ZONE();

    m_uploadBuffer->reset();

    // TODO
    //std::vector<size_t> fullyLoaded {};

    for (size_t activeIdx = 0; activeIdx < m_activeStreamingMeshes.size(); ++activeIdx) {
        StreamingMesh& streamingMesh = m_activeStreamingMeshes[activeIdx];

        switch (streamingMesh.state) {
        case MeshStreamingState::PendingAllocation: {

            bool allocSuccess = allocateVertexDataForMesh(*streamingMesh.mesh,
                                                          streamingMesh.includeIndices,
                                                          streamingMesh.includeSkinningData,
                                                          streamingMesh.includeVelocityData);

            if (allocSuccess) {
                streamingMesh.state = MeshStreamingState::LoadingData;
            }

        } break;

        case MeshStreamingState::LoadingData: {

            bool allVertexDataStreamedIn = true;

            for (StaticMeshLOD& lod : streamingMesh.mesh->LODs()) {
                for (StaticMeshSegment& meshSegment : lod.meshSegments) {

                    // TODO: Actually stream this in on the command list!
                    uploadMeshDataForAllocation(VertexUploadJob { .asset = meshSegment.asset,
                                                                  .target = &meshSegment,
                                                                  .allocation = meshSegment.vertexAllocation });

                }
            }

            if (allVertexDataStreamedIn) {
                streamingMesh.state = MeshStreamingState::CreatingBLAS;
            }

        } break;

        //case MeshStreamingState::StreamingVertexData: {
        //
        //    // todo!
        //
        //} break;

        //case MeshStreamingState::StreamingIndexData: {
        //
        //    // todo!
        //
        //} break;

        case MeshStreamingState::CreatingBLAS:{

            bool allBLASesCreated = true;

            for (StaticMeshLOD& lod : streamingMesh.mesh->LODs()) {
                for (StaticMeshSegment& meshSegment : lod.meshSegments) {

                    meshSegment.blas = createBottomLevelAccelerationStructure(meshSegment.vertexAllocation, nullptr);

                    if (!meshSegment.blas) { 
                        // Failed to create BLAS, hopefully temporarily, try again later
                        allBLASesCreated = false;
                    }
                }
            }

            if (allBLASesCreated) {
                // TODO: Compact BLAS after creation
                streamingMesh.state = MeshStreamingState::Loaded;
            }

        } break;

        // case MeshStreamingState::CompactingBLAS: {
        //
        //     // todo!
        //
        // } break;

        case MeshStreamingState::Loaded: {

            // TODO: Move to idle list! Probably do before we even end up here though..
            // fullyLoaded.push_back(activeIdx);

        } break;
        }
    }
}

bool VertexManager::allocateSkeletalMeshInstance(SkeletalMeshInstance& instance)
{
    SCOPED_PROFILE_ZONE();

    SkeletalMesh* skeletalMesh = m_scene->skeletalMeshForHandle(instance.mesh());
    if (skeletalMesh == nullptr) {
        ARKOSE_LOG(Error, "Failed to allocate skeletal mesh instance for handle {}: mesh not found", instance.mesh().index());
        return false;
    }

    StaticMesh& underlyingMesh = skeletalMesh->underlyingMesh();

    constexpr u32 lodIdx = 0;
    StaticMeshLOD& lod = underlyingMesh.lodAtIndex(lodIdx);

    for (size_t segmentIdx = 0; segmentIdx < lod.meshSegments.size(); ++segmentIdx) {
        StaticMeshSegment& meshSegment = lod.meshSegments[segmentIdx];

        if (!instance.hasSkinningVertexMappingForSegmentIndex(segmentIdx)) {
            // We don't need to allocate indices or skinning for the target. The indices will duplicate the underlying mesh
            // as it's never changed, and skinning data will never be needed for the *target*. We do have to allocate space
            // for velocity data, however, as it's something that's specific for the animated target vertices.
            constexpr bool includeIndices = false;
            constexpr bool includeSkinningData = false;
            constexpr bool includeVelocityData = true;

            VertexAllocation instanceVertexAllocation = allocateMeshDataForSegment(*meshSegment.asset,
                                                                                   includeIndices,
                                                                                   includeSkinningData,
                                                                                   includeVelocityData);

            if (!instanceVertexAllocation.isValid()) {
                ARKOSE_LOG(Error, "Failed to allocate vertex data for skeletal mesh instance: no room for segment {}", segmentIdx);
                return false;
            }

            instanceVertexAllocation.firstIndex = meshSegment.vertexAllocation.firstIndex;
            instanceVertexAllocation.indexCount = meshSegment.vertexAllocation.indexCount;

            SkinningVertexMapping skinningVertexMapping { .underlyingMesh = meshSegment.vertexAllocation,
                                                          .skinnedTarget = instanceVertexAllocation };
            instance.setSkinningVertexMapping(segmentIdx, skinningVertexMapping);
        }
    }

    // TODO: Move to a deferred step! No need to create this this very frame
    if (m_scene->maintainRayTracingScene()) {

        for (size_t segmentIdx = 0; segmentIdx < lod.meshSegments.size(); ++segmentIdx) {
            StaticMeshSegment& meshSegment = lod.meshSegments[segmentIdx];

            if (instance.hasBlasForSegmentIndex(segmentIdx)) {
                continue;
            }

            SkinningVertexMapping const& skinningVertexMappings = instance.skinningVertexMappingForSegmentIndex(segmentIdx);
            ARKOSE_ASSERT(skinningVertexMappings.skinnedTarget.isValid());

            // NOTE: We construct the new BLAS into its own buffers but 1) we don't have any data in there yet to build from,
            // and 2) we don't want to build redundantly, so we pass in the existing BLAS from the underlying mesh as a BLAS
            // copy source, which means that we copy the built BLAS into place.
            BottomLevelAS const* sourceBlas = meshSegment.blas.get();
           
            if (sourceBlas == nullptr) { 
                //ARKOSE_LOG(Info, "Source BLAS not yet available, waiting...");
                return false;
            }

            auto blas = createBottomLevelAccelerationStructure(skinningVertexMappings.skinnedTarget, sourceBlas);
            instance.setBLAS(segmentIdx, std::move(blas));
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

bool VertexManager::allocateVertexDataForMesh(StaticMesh& staticMesh, bool includeIndices, bool includeSkinningData, bool includeVelocityData)
{
    bool success = true;

    for (StaticMeshLOD& lod : staticMesh.LODs()) {
        for (StaticMeshSegment& meshSegment : lod.meshSegments) {

            if (meshSegment.vertexAllocation.isValid()) {
                continue;
            }

            VertexAllocation allocation = allocateMeshDataForSegment(*meshSegment.asset,
                                                                     includeIndices,
                                                                     includeSkinningData,
                                                                     includeVelocityData);
            if (allocation.isValid()) {
                meshSegment.vertexAllocation = allocation;
            } else {
                // No room to allocate, hopefully temporarily, try again later
                success = false;
            }
        }
    }

    return success;
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
