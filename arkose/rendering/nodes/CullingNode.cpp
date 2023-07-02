#include "CullingNode.h"

#include "core/Types.h"
#include "core/math/Frustum.h"
#include "utility/Profiling.h"
#include <imgui.h>

// Shared shader headers
#include "shaders/shared/IndirectData.h"
#include "shaders/shared/LightData.h"

void CullingNode::drawGui()
{
    if (ImGui::TreeNode("Debug##culling")) {
        ImGui::Checkbox("Frustum cull", &m_frustumCull);
        ImGui::TreePop();
    }
}

RenderPipelineNode::ExecuteCallback CullingNode::construct(GpuScene& scene, Registry& reg)
{
    // todo: maybe default to smaller, and definitely actually grow when needed!
    static constexpr size_t initialBufferCount = 16 * 1024;

    Buffer& frustumPlaneBuffer = reg.createBuffer(6 * sizeof(vec4), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::TransferOptimal);
    Buffer& indirectDrawableBuffer = reg.createBuffer(initialBufferCount * sizeof(IndirectShaderDrawable), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::TransferOptimal);

    //////////////
    // Opaque

    Buffer& opaqueDrawableBuffer = reg.createBuffer(initialBufferCount * sizeof(ShaderDrawable), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOnly);
    opaqueDrawableBuffer.setName("MainViewCulledDrawablesOpaque");
    BindingSet& opaqueDrawableBindingSet = reg.createBindingSet({ ShaderBinding::storageBuffer(opaqueDrawableBuffer, ShaderStage::Vertex) });
    reg.publish("MainViewCulledDrawablesOpaqueSet", opaqueDrawableBindingSet);

    Buffer& opaqueDrawsCmdBuffer = reg.createBuffer(initialBufferCount * sizeof(IndexedDrawCmd), Buffer::Usage::IndirectBuffer, Buffer::MemoryHint::GpuOnly);
    reg.publish("MainViewOpaqueDrawCmds", opaqueDrawsCmdBuffer);
    Buffer& opaqueDrawCountBuffer = reg.createBuffer(sizeof(uint), Buffer::Usage::IndirectBuffer, Buffer::MemoryHint::TransferOptimal);
    reg.publish("MainViewOpaqueDrawCount", opaqueDrawCountBuffer);

    //////////////
    // Masked

    Buffer& maskedDrawableBuffer = reg.createBuffer(initialBufferCount * sizeof(ShaderDrawable), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOnly);
    maskedDrawableBuffer.setName("MainViewCulledDrawablesMasked");
    BindingSet& maskedDrawableBindingSet = reg.createBindingSet({ ShaderBinding::storageBuffer(maskedDrawableBuffer, ShaderStage::Vertex) });
    reg.publish("MainViewCulledDrawablesMaskedSet", maskedDrawableBindingSet);

    Buffer& maskedDrawsCmdBuffer = reg.createBuffer(initialBufferCount * sizeof(IndexedDrawCmd), Buffer::Usage::IndirectBuffer, Buffer::MemoryHint::GpuOnly);
    reg.publish("MainViewMaskedDrawCmds", maskedDrawsCmdBuffer);
    Buffer& maskedDrawCountBuffer = reg.createBuffer(sizeof(uint), Buffer::Usage::IndirectBuffer, Buffer::MemoryHint::TransferOptimal);
    reg.publish("MainViewMaskedDrawCount", maskedDrawCountBuffer);

    //////////////
    // 

    BindingSet& cullingBindingSet = reg.createBindingSet({ ShaderBinding::storageBuffer(frustumPlaneBuffer, ShaderStage::Compute),
                                                           ShaderBinding::storageBuffer(indirectDrawableBuffer, ShaderStage::Compute),
                                                           ShaderBinding::storageBuffer(opaqueDrawableBuffer, ShaderStage::Compute),
                                                           ShaderBinding::storageBuffer(opaqueDrawsCmdBuffer, ShaderStage::Compute),
                                                           ShaderBinding::storageBuffer(opaqueDrawCountBuffer, ShaderStage::Compute),
                                                           ShaderBinding::storageBuffer(maskedDrawableBuffer, ShaderStage::Compute),
                                                           ShaderBinding::storageBuffer(maskedDrawsCmdBuffer, ShaderStage::Compute),
                                                           ShaderBinding::storageBuffer(maskedDrawCountBuffer, ShaderStage::Compute) });

    ComputeState& cullingState = reg.createComputeState(Shader::createCompute("culling/culling.comp"), { &cullingBindingSet });
    cullingState.setName("MainViewCulling");

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        size_t planesByteSize;
        const geometry::Plane* planesData = scene.camera().frustum().rawPlaneData(&planesByteSize);
        ARKOSE_ASSERT(planesByteSize == frustumPlaneBuffer.size());
        uploadBuffer.upload(planesData, planesByteSize, frustumPlaneBuffer);

        std::vector<IndirectShaderDrawable> indirectDrawableData {};

        size_t numInputDrawables = 0;

        for (auto& instance : scene.staticMeshInstances()) {
            if (const StaticMesh* staticMesh = scene.staticMeshForInstance(*instance)) {

                // TODO: Pick LOD properly
                const StaticMeshLOD& lod = staticMesh->lodAtIndex(0);

                // TODO: Culling (e.g. frustum) should be done on mesh/LOD level, not per segment, but this will work for now
                for (u32 segmentIdx = 0; segmentIdx < lod.meshSegments.size(); ++segmentIdx) {

                    StaticMeshSegment const& meshSegment = lod.meshSegments[segmentIdx];

                    ShaderMaterial const& material = *scene.materialForHandle(meshSegment.material);
                    ShaderDrawable const& drawable = *scene.drawableForHandle(instance->drawableHandleForSegmentIndex(segmentIdx));

                    DrawCallDescription drawCall = meshSegment.vertexAllocation.asDrawCallDescription();
                    indirectDrawableData.push_back({ .drawable = drawable,
                                                     .localBoundingSphere = vec4(staticMesh->boundingSphere().center(), staticMesh->boundingSphere().radius()),
                                                     .indexCount = drawCall.indexCount,
                                                     .firstIndex = drawCall.firstIndex,
                                                     .vertexOffset = drawCall.vertexOffset,
                                                     .materialBlendMode = material.blendMode });

                    numInputDrawables += 1;
                }
            }
        }

        for (auto& instance : scene.skeletalMeshInstances()) {
            if (SkeletalMesh const* skeletalMesh = scene.skeletalMeshForInstance(*instance)) {
                StaticMesh const& underlyingMesh = skeletalMesh->underlyingMesh();

                // TODO: Pick LOD properly
                const StaticMeshLOD& lod = underlyingMesh.lodAtIndex(0);

                // TODO: Culling (e.g. frustum) should be done on mesh/LOD level, not per segment, but this will work for now
                for (u32 segmentIdx = 0; segmentIdx < lod.meshSegments.size(); ++segmentIdx) {

                    StaticMeshSegment const& meshSegment = lod.meshSegments[segmentIdx];
                    SkinningVertexMapping const& skinningVertexMapping = instance->skinningVertexMappingForSegmentIndex(segmentIdx);

                    ShaderDrawable const& drawable = *scene.drawableForHandle(instance->drawableHandleForSegmentIndex(segmentIdx));

                    // TODO: Make materialBlendMode into a u32 (because it really should be)!
                    indirectDrawableData.push_back({ .drawable = drawable,
                                                     .localBoundingSphere = vec4(underlyingMesh.boundingSphere().center(), underlyingMesh.boundingSphere().radius()),
                                                     .indexCount = skinningVertexMapping.skinnedTarget.indexCount,
                                                     .firstIndex = skinningVertexMapping.skinnedTarget.firstIndex,
                                                     .vertexOffset = static_cast<i32>(skinningVertexMapping.skinnedTarget.firstVertex),
                                                     .materialBlendMode = static_cast<i32>(blendModeToShaderBlendMode(meshSegment.blendMode)) });

                    numInputDrawables += 1;

                }
            }
        }

        size_t newSize = numInputDrawables * sizeof(IndirectShaderDrawable);
        ARKOSE_ASSERT(newSize <= indirectDrawableBuffer.size()); // fixme: grow instead of failing!
        uploadBuffer.upload(indirectDrawableData, indirectDrawableBuffer);

        uint32_t zero = 0u;
        uploadBuffer.upload(zero, opaqueDrawCountBuffer);
        uploadBuffer.upload(zero, maskedDrawCountBuffer);

        cmdList.executeBufferCopyOperations(uploadBuffer);

        cmdList.setComputeState(cullingState);
        cmdList.bindSet(cullingBindingSet, 0);
        cmdList.setNamedUniform<uint32_t>("numInputDrawables", static_cast<uint32_t>(numInputDrawables));
        cmdList.setNamedUniform<bool>("frustumCull", m_frustumCull);
        cmdList.dispatch(Extent3D(static_cast<uint32_t>(numInputDrawables), 1, 1), Extent3D(64, 1, 1));

        // It would be nice if we could do GPU readback from last frame's count buffer (on the other hand, we do have renderdoc for this)
        //ImGui::Text("Issued draw calls: %i", numDrawCallsIssued); (in gui())
    };
}
