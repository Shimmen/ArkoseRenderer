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
#include "rendering/meshlet/MeshletView.h"
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

    if (m_scene->maintainMeshShadingScene()) {

        size_t vertexIndirectionBufferSize = sizeof(u32) * VertexManager::MaxLoadedVertices;
        size_t meshletIndexBufferSize = sizeof(u32) * VertexManager::MaxLoadedIndices;
        size_t meshletBufferSize = sizeof(ShaderMeshlet) * MaxLoadedMeshlets;

        float totalMeshletMemoryUseMb = ark::conversion::to::MB(vertexIndirectionBufferSize + meshletIndexBufferSize + meshletBufferSize);
        ARKOSE_LOG(Info, "VertexManager: allocating a total of {:.1f} MB of VRAM for meshlet data", totalMeshletMemoryUseMb);

        m_meshletVertexIndirectionBuffer = backend.createBuffer(vertexIndirectionBufferSize, Buffer::Usage::StorageBuffer);
        m_meshletVertexIndirectionBuffer->setStride(sizeof(u32));
        m_meshletVertexIndirectionBuffer->setName("SceneMeshletVertexIndirectionData");

        m_meshletIndexBuffer = backend.createBuffer(meshletIndexBufferSize, Buffer::Usage::Index);
        m_meshletIndexBuffer->setStride(sizeof(u32));
        m_meshletIndexBuffer->setName("SceneMeshletIndexData");

        m_meshletBuffer = backend.createBuffer(meshletBufferSize, Buffer::Usage::StorageBuffer);
        m_meshletBuffer->setStride(sizeof(ShaderMeshlet));
        m_meshletBuffer->setName("SceneMeshletData");
    }

    m_uploadBuffer = std::make_unique<UploadBuffer>(backend, UploadBufferSize);
}

VertexManager::~VertexManager()
{
}

void VertexManager::registerForStreaming(StaticMesh& mesh, bool includeIndices, bool includeSkinningData)
{
    // There are (currently) no cases where we have velocity data from an asset that we need to upload
    constexpr bool includeVelocityData = false;

    m_streamingMeshes.push_back(StreamingMesh { .mesh = &mesh,
                                                .state = MeshStreamingState::PendingAllocation,
                                                .includeIndices = includeIndices,
                                                .includeSkinningData = includeSkinningData,
                                                .includeVelocityData = includeVelocityData });
}

template<typename F>
bool VertexManager::processStreamingMeshState(StreamingMesh& streamingMesh, F&& processSegmentCallback)
{
    StaticMesh* mesh = streamingMesh.mesh;

    for (; streamingMesh.nextLOD < mesh->LODs().size(); streamingMesh.nextLOD++) {
        StaticMeshLOD& lod = mesh->lodAtIndex(streamingMesh.nextLOD);

        for (; streamingMesh.nextSegment < lod.meshSegments.size(); streamingMesh.nextSegment++) {
            StaticMeshSegment& meshSegment = lod.meshSegments[streamingMesh.nextSegment];

            bool success = processSegmentCallback(meshSegment);

            if (!success) {
                return false;
            }
        }
    }

    return true;
}

void VertexManager::processMeshStreaming(CommandList& cmdList, std::unordered_set<StaticMeshHandle>& updatedMeshes)
{
    SCOPED_PROFILE_ZONE();

    m_uploadBuffer->reset();

    for (size_t activeIdx = 0; activeIdx < m_streamingMeshes.size(); ++activeIdx) {
        StreamingMesh& streamingMesh = m_streamingMeshes[activeIdx];

        switch (streamingMesh.state) {
        case MeshStreamingState::PendingAllocation: {

            bool stateDone = processStreamingMeshState(streamingMesh, [&](StaticMeshSegment& meshSegment) -> bool {
                VertexAllocation allocation = allocateMeshDataForSegment(*meshSegment.asset,
                                                                         streamingMesh.includeIndices,
                                                                         streamingMesh.includeSkinningData,
                                                                         streamingMesh.includeVelocityData);
                if (allocation.isValid()) {
                    // TODO: Consider if we want to hold off on assigning this until we have a mesh that can actually be drawn to.
                    // It's currently allocated for, but nothing is actually streamed in, so it's a bit of a dud for the moment.
                    meshSegment.vertexAllocation = allocation;
                    return true;
                } else {
                    // No room to allocate, hopefully temporarily, try again later
                    return false;
                }
            });

            if (stateDone) {
                streamingMesh.setNextState(MeshStreamingState::LoadingData);
            }

        } break;

        case MeshStreamingState::LoadingData: {

            bool allVertexDataStreamedIn = true;

            for (StaticMeshLOD& lod : streamingMesh.mesh->LODs()) {
                for (StaticMeshSegment& meshSegment : lod.meshSegments) {

                    // TODO: Actually stream this in on the command list!
                    uploadMeshDataForAllocation(*meshSegment.asset, meshSegment.vertexAllocation);

                }
            }

            if (allVertexDataStreamedIn) {
                if ( m_scene->maintainMeshShadingScene() ) {
                    streamingMesh.setNextState(MeshStreamingState::StreamingMeshletData);
                } else if (m_scene->maintainRayTracingScene()) {
                    streamingMesh.setNextState(MeshStreamingState::CreatingBLAS);
                } else {
                    streamingMesh.setNextState(MeshStreamingState::Loaded);
                }
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

        case MeshStreamingState::StreamingMeshletData: {

            ARKOSE_ASSERT(m_scene->maintainMeshShadingScene());

            bool stateDone = processStreamingMeshState(streamingMesh, [&](StaticMeshSegment& meshSegment) -> bool {
                meshSegment.meshletView = streamMeshletDataForSegment(streamingMesh, meshSegment);

                if (meshSegment.meshletView) {
                    // Signal to the caller that the mesh has changed
                    updatedMeshes.insert(meshSegment.staticMeshHandle);
                    return true;
                } else {
                    return false;
                }
            });

            if (stateDone) {
                if (m_scene->maintainRayTracingScene()) {
                    streamingMesh.setNextState(MeshStreamingState::CreatingBLAS);
                } else {
                    streamingMesh.setNextState(MeshStreamingState::Loaded);
                }
            }

        } break;

        case MeshStreamingState::CreatingBLAS: {

            bool stateDone = processStreamingMeshState(streamingMesh, [&](StaticMeshSegment& meshSegment) -> bool {
                meshSegment.blas = createBottomLevelAccelerationStructure(meshSegment.vertexAllocation);
                if (meshSegment.blas != nullptr) {
                    cmdList.buildBottomLevelAcceratationStructure(*meshSegment.blas, AccelerationStructureBuildType::FullBuild);
                    return true;
                } else {
                    return false;
                }
            });

            if (stateDone) {
                streamingMesh.setNextState(MeshStreamingState::CompactingBLAS);
            }

        } break;

        case MeshStreamingState::CompactingBLAS: {

            bool stateDone = processStreamingMeshState(streamingMesh, [&](StaticMeshSegment& meshSegment) -> bool {
                return cmdList.compactBottomLevelAcceratationStructure(*meshSegment.blas);
            });

            if (stateDone) { 
                streamingMesh.setNextState(MeshStreamingState::Loaded);
            }

        } break;

        case MeshStreamingState::Loaded: {

            // Nothing to do here...

        } break;
        }
    }

    if (m_uploadBuffer->peekPendingOperations().size() > 0) {
        cmdList.executeBufferCopyOperations(*m_uploadBuffer);
    }
}

void VertexManager::drawUI() const
{
    if (ImGui::BeginTabBar("VertexManagerTabBar")) {

        if (ImGui::BeginTabItem("Streaming meshes")) {
            ImGui::BeginChild("StreamingMeshesChild");
            if (ImGui::BeginTable("StreamingMeshesTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Streaming mesh", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableHeadersRow();
                for (StreamingMesh const& streamingMesh : m_streamingMeshes) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", streamingMesh.mesh->name().data());
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", magic_enum::enum_name(streamingMesh.state).data());
                }
                ImGui::EndTable();
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Vertex allocations")) {
            ImGui::BeginChild("VertexAllocationsChild");

            float totalAllocatedSizeMB = 0.0f;
            float totalUsedSizeMB = 0.0f;

            if (ImGui::BeginTable("MeshIndexDataVramUsageTable", 6)) {

                ImGui::TableSetupColumn("Index type", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Used #", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn("Cap. #", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn("%", ImGuiTableColumnFlags_WidthFixed, 40.0f);
                ImGui::TableSetupColumn("Used size (MB)", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableSetupColumn("Allocated size (MB)", ImGuiTableColumnFlags_WidthFixed, 140.0f);

                ImGui::TableHeadersRow();

                auto doTableRow = [&](Buffer const& indexBuffer, IndexType indexType, u32 numUsedIndices) {
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%s", indexTypeToString(indexType));

                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%u", numUsedIndices);

                    ImGui::TableSetColumnIndex(2);
                    u32 totalNumVertices = narrow_cast<u32>(indexBuffer.size() / sizeofIndexType(indexType));
                    ImGui::Text("%u", totalNumVertices);

                    ImGui::TableSetColumnIndex(3);
                    f32 percentageFilled = 100.0f * static_cast<f32>(numUsedIndices) / static_cast<f32>(totalNumVertices);
                    ImGui::Text("%.1f%%", percentageFilled);

                    ImGui::TableSetColumnIndex(4);
                    float usedSizeMB = ark::conversion::to::MB(numUsedIndices * sizeofIndexType(indexType));
                    ImGui::Text("%.2f MB", usedSizeMB);

                    ImGui::TableSetColumnIndex(5);
                    float allocatedSizeMB = ark::conversion::to::MB(indexBuffer.size());
                    ImGui::Text("%.2f MB", allocatedSizeMB);

                    totalAllocatedSizeMB += allocatedSizeMB;
                    totalUsedSizeMB += usedSizeMB;
                };

                doTableRow(indexBuffer(), indexType(), numAllocatedIndices());

                ImGui::EndTable();
            }

            ImGui::Spacing();

            if (ImGui::BeginTable("MeshVertexDataVramUsageTable", 6)) {

                ImGui::TableSetupColumn("Vertex layout", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Used #", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn("Cap. #", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn("%", ImGuiTableColumnFlags_WidthFixed, 40.0f);
                ImGui::TableSetupColumn("Used size (MB)", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableSetupColumn("Allocated size (MB)", ImGuiTableColumnFlags_WidthFixed, 140.0f);

                ImGui::TableHeadersRow();

                auto doTableRow = [&](Buffer const& vertexBuffer, VertexLayout const& vertexLayout, u32 numUsedVertices) {
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    std::string layoutDescription = vertexLayout.toString(false);
                    ImGui::Text("%s", layoutDescription.c_str());

                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%u", numUsedVertices);

                    ImGui::TableSetColumnIndex(2);
                    u32 totalNumVertices = narrow_cast<u32>(vertexBuffer.size() / vertexLayout.packedVertexSize());
                    ImGui::Text("%u", totalNumVertices);

                    ImGui::TableSetColumnIndex(3);
                    f32 percentageFilled = 100.0f * static_cast<f32>(numUsedVertices) / static_cast<f32>(totalNumVertices);
                    ImGui::Text("%.1f%%", percentageFilled);

                    ImGui::TableSetColumnIndex(4);
                    float usedSizeMB = ark::conversion::to::MB(numUsedVertices * vertexLayout.packedVertexSize());
                    ImGui::Text("%.2f MB", usedSizeMB);

                    ImGui::TableSetColumnIndex(5);
                    float allocatedSizeMB = ark::conversion::to::MB(vertexBuffer.size());
                    ImGui::Text("%.2f MB", allocatedSizeMB);

                    totalAllocatedSizeMB += allocatedSizeMB;
                    totalUsedSizeMB += usedSizeMB;
                };

                doTableRow(positionVertexBuffer(), positionVertexLayout(), numAllocatedVertices());
                doTableRow(nonPositionVertexBuffer(), nonPositionVertexLayout(), numAllocatedVertices());
                doTableRow(skinningDataVertexBuffer(), skinningDataVertexLayout(), numAllocatedSkinningVertices());
                doTableRow(velocityDataVertexBuffer(), velocityDataVertexLayout(), numAllocatedVelocityVertices());

                ImGui::EndTable();
            }

            ImGui::Spacing();

            float totalAllocatedMeshletSizeMB = 0.0f;
            float totalUsedMeshletSizeMB = 0.0f;
            if (m_scene->maintainMeshShadingScene()) {
                if (ImGui::BeginTable("MeshletDataVramUsageTable", 6)) {

                    ImGui::TableSetupColumn("Meshlet data", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Used #", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                    ImGui::TableSetupColumn("Cap. #", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                    ImGui::TableSetupColumn("%", ImGuiTableColumnFlags_WidthFixed, 40.0f);
                    ImGui::TableSetupColumn("Used size (MB)", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                    ImGui::TableSetupColumn("Allocated size (MB)", ImGuiTableColumnFlags_WidthFixed, 140.0f);

                    ImGui::TableHeadersRow();

                    auto doTableRow = [&](char const* label, Buffer const& buffer, u32 numUsed) {
                        ImGui::TableNextRow();

                        ImGui::TableSetColumnIndex(0);
                        ImGui::Text("%s", label);

                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%u", numUsed);

                        ImGui::TableSetColumnIndex(2);
                        u32 totalNum = narrow_cast<u32>(buffer.size() / buffer.stride());
                        ImGui::Text("%u", totalNum);

                        ImGui::TableSetColumnIndex(3);
                        f32 percentageFilled = 100.0f * static_cast<f32>(numUsed) / static_cast<f32>(totalNum);
                        ImGui::Text("%.1f%%", percentageFilled);

                        ImGui::TableSetColumnIndex(4);
                        float usedSizeMB = ark::conversion::to::MB(numUsed * buffer.stride());
                        ImGui::Text("%.2f MB", usedSizeMB);

                        ImGui::TableSetColumnIndex(5);
                        float allocatedSizeMB = ark::conversion::to::MB(buffer.size());
                        ImGui::Text("%.2f MB", allocatedSizeMB);

                        totalAllocatedMeshletSizeMB += allocatedSizeMB;
                        totalUsedMeshletSizeMB += usedSizeMB;
                    };

                    doTableRow("Meshlets", *m_meshletBuffer, m_nextFreeMeshletIndex);
                    doTableRow("Meshlet vertex indirection", *m_meshletVertexIndirectionBuffer, m_nextFreeMeshletIndirIndex);
                    doTableRow("Meshlet indices", *m_meshletIndexBuffer, m_nextFreeMeshletIndexBufferIndex);

                    ImGui::EndTable();
                }
            }

            ImGui::Separator();
            ImGui::Text("Total index + vertex: %.2f MB / %.2f MB", totalUsedSizeMB, totalAllocatedSizeMB);
            if (m_scene->maintainMeshShadingScene()) {
                ImGui::Text("Total meshlet:        %.2f MB / %.2f MB", totalUsedMeshletSizeMB, totalAllocatedMeshletSizeMB);
            }

            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}

bool VertexManager::allocateSkeletalMeshInstance(SkeletalMeshInstance& instance, CommandList& cmdList)
{
    SCOPED_PROFILE_ZONE();

    SkeletalMesh* skeletalMesh = m_scene->skeletalMeshForHandle(instance.mesh());
    if (skeletalMesh == nullptr) {
        ARKOSE_LOG(Error, "Failed to allocate skeletal mesh instance for handle {}: mesh not found", instance.mesh().index());
        return false;
    }

    StreamingSkeletalMesh& streamingSkeletalMesh = m_streamingSkeletalMeshes.emplace_back();
    streamingSkeletalMesh.skeletalMeshInstance = &instance;

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

            // Indices not included in allocation as the data is static; simply use the underlying mesh indices
            instanceVertexAllocation.firstIndex = meshSegment.vertexAllocation.firstIndex;
            instanceVertexAllocation.indexCount = meshSegment.vertexAllocation.indexCount;

            SkinningVertexMapping skinningVertexMapping { .underlyingMesh = meshSegment.vertexAllocation,
                                                          .skinnedTarget = instanceVertexAllocation };
            instance.setSkinningVertexMapping(segmentIdx, skinningVertexMapping);

            // Track owning allocations so we can free them later
            streamingSkeletalMesh.owningAllocations.push_back(instanceVertexAllocation.internalAllocations);
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

            auto blas = createBottomLevelAccelerationStructure(skinningVertexMappings.skinnedTarget);

            if (blas) {
                cmdList.copyBottomLevelAcceratationStructure(*blas, *sourceBlas);
                instance.setBLAS(segmentIdx, std::move(blas));
            } else {
                ARKOSE_LOG(Info, "Failed to create BLAS for skeletal mesh");
                return false;
            }
        }
    }

    return true;
}

VertexAllocation VertexManager::allocateMeshDataForSegment(MeshSegmentAsset const& segmentAsset, bool includeIndices, bool includeSkinningData, bool includeVelocityData)
{
    SCOPED_PROFILE_ZONE();

    u32 vertexCount = narrow_cast<u32>(segmentAsset.vertexCount());
    u32 indexCount = narrow_cast<u32>(segmentAsset.indices.size());

    // Allocate all the vertex data...

    VertexAllocation::Internal allocs;

    allocs.vertexAlloc = m_vertexAllocator.allocate(vertexCount);
    if (!allocs.vertexAlloc.isValid()) {
        return {};
    }

    if (indexCount > 0 && includeIndices) {
        allocs.indexAlloc = m_indexAllocator.allocate(indexCount);
        if (!allocs.indexAlloc.isValid()) {
            if (allocs.vertexAlloc.isValid()) m_vertexAllocator.free(allocs.vertexAlloc);
            return {};
        }
    }

    if (segmentAsset.hasSkinningData() && includeSkinningData) {
        allocs.skinningVertAlloc = m_skinningVertexAllocator.allocate(vertexCount);
        if (!allocs.skinningVertAlloc.isValid()) {
            if (allocs.vertexAlloc.isValid()) m_vertexAllocator.free(allocs.vertexAlloc);
            if (allocs.indexAlloc.isValid()) m_indexAllocator.free(allocs.indexAlloc);
            return {};
        }
    }

    if (includeVelocityData) {
        allocs.velocityVertAlloc = m_velocityVertexAllocator.allocate(vertexCount);
        if (!allocs.velocityVertAlloc.isValid()) {
            if (allocs.vertexAlloc.isValid()) m_vertexAllocator.free(allocs.vertexAlloc);
            if (allocs.indexAlloc.isValid()) m_indexAllocator.free(allocs.indexAlloc);
            if (allocs.skinningVertAlloc.isValid()) m_skinningVertexAllocator.free(allocs.skinningVertAlloc);
            return {};
        }
    }

    // If all requested allocations succeeded, return the VertexAllocation for the segment

    VertexAllocation allocation {};
    allocation.internalAllocations = allocs;

    allocation.firstVertex = allocs.vertexAlloc.offset;
    allocation.vertexCount = vertexCount;

    if (indexCount > 0 && includeIndices) {
        allocation.firstIndex = allocs.indexAlloc.offset;
        allocation.indexCount = indexCount;
    }

    if (segmentAsset.hasSkinningData() && includeSkinningData) {
        allocation.firstSkinningVertex = allocs.skinningVertAlloc.offset;
    }

    if (includeVelocityData) {
        allocation.firstVelocityVertex = allocs.velocityVertAlloc.offset;
    }

    ARKOSE_ASSERT(allocation.isValid());
    return allocation;
}

void VertexManager::uploadMeshDataForAllocation(MeshSegmentAsset const& segmentAsset, VertexAllocation const& allocation)
{
    SCOPED_PROFILE_ZONE();

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
    if (allocation.hasSkinningData()) {
        ARKOSE_ASSERT(segmentAsset.hasSkinningData());
        ARKOSE_ASSERT(segmentAsset.jointIndices.size() == segmentAsset.jointWeights.size());
        std::vector<u8> skinningVertexData = segmentAsset.assembleVertexData(m_skinningDataVertexLayout);
        size_t skinningDataOffset = allocation.firstSkinningVertex * m_skinningDataVertexLayout.packedVertexSize();
        m_skinningDataVertexBuffer->updateData(skinningVertexData.data(), skinningVertexData.size(), skinningDataOffset);
    }

    // Upload index data if relevant
    if (allocation.hasIndices()) {
        size_t indexSize = sizeofIndexType(indexType());
        size_t indexOffset = allocation.firstIndex * indexSize;
        m_indexBuffer->updateData(segmentAsset.indices.data(), segmentAsset.indices.size() * indexSize, indexOffset);
    }
}

std::optional<MeshletView> VertexManager::streamMeshletDataForSegment(StreamingMesh& streamingMesh, StaticMeshSegment const& meshSegment)
{
    MeshSegmentAsset const& meshSegmentAsset = *meshSegment.asset;
    MeshletDataAsset const& meshletDataAsset = meshSegmentAsset.meshletData.value();

    //
    // Test stuff (should not be needed, remove me)
    //

    u32 vertexCount = narrow_cast<u32>(meshletDataAsset.meshletVertexIndirection.size());
    u32 indexCount = narrow_cast<u32>(meshletDataAsset.meshletIndices.size());
    u32 meshletCount = narrow_cast<u32>(meshletDataAsset.meshlets.size());

    size_t totalUploadSize = vertexCount * sizeof(u32) // vertex indirection buffer
        + indexCount * sizeof(u32) // index buffer
        + meshletCount * sizeof(ShaderMeshlet); // meshlet buffer

    // TODO: There are instances where segments are massive, so we need to allow uploading with a finer granularity.
    if (totalUploadSize > m_uploadBuffer->remainingSize()) {
        if (totalUploadSize > UploadBufferSize) {
            ARKOSE_LOG(Fatal, "Static mesh segment is {:.2f} MB but the meshlet upload budget is only {:.2f} MB. "
                              "The budget must be increased if we want to be able to load this asset.",
                       ark::conversion::to::MB(totalUploadSize), ark::conversion::to::MB(UploadBufferSize));
        }
        return std::nullopt;
    }

    //
    // Initial data prep
    //

    // Offset indices by current vertex count as we put all meshlets in a single buffer
    std::vector<u32> adjustedMeshletIndices = meshletDataAsset.meshletIndices;
    for (u32& index : adjustedMeshletIndices) {
        index += m_nextFreeMeshletIndirIndex;
    }

    // Offset vertex indirection by the segment's first vertex as we're referencing the global vertex buffers
    std::vector<u32> adjustedVertexIndirection = meshletDataAsset.meshletVertexIndirection;
    for (u32& vertexIndex : adjustedVertexIndirection) {
        vertexIndex += meshSegment.vertexAllocation.firstVertex;
    }

    //
    // Stream meshlet vertex indirection data
    //

    size_t vertexIndirectionOffset = m_nextFreeMeshletIndirIndex * sizeof(u32);
    m_uploadBuffer->upload(adjustedVertexIndirection, *m_meshletVertexIndirectionBuffer, vertexIndirectionOffset);

    //
    // Stream meshlet index data
    //

    size_t indexDataOffset = m_nextFreeMeshletIndexBufferIndex * sizeof(u32);
    m_uploadBuffer->upload(adjustedMeshletIndices, *m_meshletIndexBuffer, indexDataOffset);

    //
    // Stream meshlet data
    //

    for (MeshletAsset const& meshletAsset : meshletDataAsset.meshlets) {

        ShaderMeshlet meshlet { .firstIndex = m_nextFreeMeshletIndexBufferIndex + meshletAsset.firstIndex,
                                .triangleCount = meshletAsset.triangleCount,
                                .firstVertex = m_nextFreeMeshletIndirIndex,
                                .vertexCount = meshletAsset.vertexCount,
                                .center = meshletAsset.center,
                                .radius = meshletAsset.radius };

        m_meshlets.push_back(meshlet);
        m_nextFreeMeshletIndirIndex += meshletAsset.vertexCount;
    }

    size_t meshletDataDstOffset = m_nextFreeMeshletIndex * sizeof(ShaderMeshlet);
    m_uploadBuffer->upload(m_meshlets.data() + m_nextFreeMeshletIndex, meshletCount * sizeof(ShaderMeshlet), *m_meshletBuffer, meshletDataDstOffset);

    //
    // Finalize
    //

    MeshletView meshletView = { .firstMeshlet = m_nextFreeMeshletIndex,
                                .meshletCount = meshletCount };

    m_nextFreeMeshletIndexBufferIndex += indexCount;
    m_nextFreeMeshletIndex += meshletCount;

    return meshletView;
}

std::unique_ptr<BottomLevelAS> VertexManager::createBottomLevelAccelerationStructure(VertexAllocation const& vertexAllocation)
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

    return m_backend->createBottomLevelAccelerationStructure({ geometry });
}
