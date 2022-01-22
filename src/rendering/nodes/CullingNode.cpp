#include "CullingNode.h"

#include "SceneNode.h"
#include "math/Frustum.h"
#include "utility/Logging.h"
#include "utility/Profiling.h"
#include <imgui.h>

// Shared shader headers
using uint = uint32_t;
#include "IndirectData.h"
#include "LightData.h"

RenderPipelineNode::ExecuteCallback CullingNode::construct(Scene& scene, Registry& reg)
{
    // todo: maybe default to smaller, and definitely actually grow when needed!
    static constexpr size_t initialBufferCount = 1024;

    Buffer& frustumPlaneBuffer = reg.createBuffer(6 * sizeof(vec4), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::TransferOptimal);
    Buffer& indirectDrawableBuffer = reg.createBuffer(initialBufferCount * sizeof(IndirectShaderDrawable), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::TransferOptimal);

    //////////////
    // Opaque

    Buffer& opaqueDrawableBuffer = reg.createBuffer(initialBufferCount * sizeof(ShaderDrawable), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOnly);
    opaqueDrawableBuffer.setName("MainViewCulledDrawablesOpaque");
    BindingSet& opaqueDrawableBindingSet = reg.createBindingSet({ { 0, ShaderStageVertex, &opaqueDrawableBuffer } });
    reg.publish("MainViewCulledDrawablesOpaqueSet", opaqueDrawableBindingSet);

    Buffer& opaqueDrawsCmdBuffer = reg.createBuffer(initialBufferCount * sizeof(IndexedDrawCmd), Buffer::Usage::IndirectBuffer, Buffer::MemoryHint::GpuOnly);
    reg.publish("MainViewOpaqueDrawCmds", opaqueDrawsCmdBuffer);
    Buffer& opaqueDrawCountBuffer = reg.createBuffer(sizeof(uint), Buffer::Usage::IndirectBuffer, Buffer::MemoryHint::TransferOptimal);
    reg.publish("MainViewOpaqueDrawCount", opaqueDrawCountBuffer);

    //////////////
    // Masked

    Buffer& maskedDrawableBuffer = reg.createBuffer(initialBufferCount * sizeof(ShaderDrawable), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOnly);
    maskedDrawableBuffer.setName("MainViewCulledDrawablesMasked");
    BindingSet& maskedDrawableBindingSet = reg.createBindingSet({ { 0, ShaderStageVertex, &maskedDrawableBuffer } });
    reg.publish("MainViewCulledDrawablesMaskedSet", maskedDrawableBindingSet);

    Buffer& maskedDrawsCmdBuffer = reg.createBuffer(initialBufferCount * sizeof(IndexedDrawCmd), Buffer::Usage::IndirectBuffer, Buffer::MemoryHint::GpuOnly);
    reg.publish("MainViewMaskedDrawCmds", maskedDrawsCmdBuffer);
    Buffer& maskedDrawCountBuffer = reg.createBuffer(sizeof(uint), Buffer::Usage::IndirectBuffer, Buffer::MemoryHint::TransferOptimal);
    reg.publish("MainViewMaskedDrawCount", maskedDrawCountBuffer);

    //////////////
    // 

    BindingSet& cullingBindingSet = reg.createBindingSet({ { 0, ShaderStageCompute, &frustumPlaneBuffer },
                                                           { 1, ShaderStageCompute, &indirectDrawableBuffer },
                                                           { 2, ShaderStageCompute, &opaqueDrawableBuffer },
                                                           { 3, ShaderStageCompute, &opaqueDrawsCmdBuffer },
                                                           { 4, ShaderStageCompute, &opaqueDrawCountBuffer },
                                                           { 5, ShaderStageCompute, &maskedDrawableBuffer },
                                                           { 6, ShaderStageCompute, &maskedDrawsCmdBuffer },
                                                           { 7, ShaderStageCompute, &maskedDrawCountBuffer } });

    ComputeState& cullingState = reg.createComputeState(Shader::createCompute("culling/culling.comp"), { &cullingBindingSet });
    cullingState.setName("MainViewCulling");

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        mat4 cameraViewProjection = scene.camera().viewProjectionMatrix();
        auto cameraFrustum = geometry::Frustum::createFromProjectionMatrix(cameraViewProjection);
        size_t planesByteSize;
        const geometry::Plane* planesData = cameraFrustum.rawPlaneData(&planesByteSize);
        ASSERT(planesByteSize == frustumPlaneBuffer.size());
        uploadBuffer.upload(planesData, planesByteSize, frustumPlaneBuffer);

        std::vector<IndirectShaderDrawable> indirectDrawableData {};
        int numInputDrawables = scene.forEachMesh([&](size_t, Mesh& mesh) {
            DrawCallDescription drawCall = mesh.drawCallDescription({ VertexComponent::Position3F }, scene);
            indirectDrawableData.push_back({ .drawable = { .worldFromLocal = mesh.transform().worldMatrix(),
                                                           .worldFromTangent = mat4(mesh.transform().worldNormalMatrix()),
                                                           .previousFrameWorldFromLocal = mesh.transform().previousFrameWorldMatrix(),
                                                           .materialIndex = mesh.materialIndex().value_or(0) },
                                             .localBoundingSphere = vec4(mesh.boundingSphere().center(), mesh.boundingSphere().radius()),
                                             .indexCount = drawCall.indexCount,
                                             .firstIndex = drawCall.firstIndex,
                                             .vertexOffset = drawCall.vertexOffset,
                                             .materialBlendMode = mesh.material().blendModeValue() });
        });
        size_t newSize = numInputDrawables * sizeof(IndirectShaderDrawable);
        ASSERT(newSize <= indirectDrawableBuffer.size()); // fixme: grow instead of failing!
        uploadBuffer.upload(indirectDrawableData, indirectDrawableBuffer);

        uint32_t zero = 0u;
        uploadBuffer.upload(zero, opaqueDrawCountBuffer);
        uploadBuffer.upload(zero, maskedDrawCountBuffer);

        cmdList.executeBufferCopyOperations(uploadBuffer);

        cmdList.setComputeState(cullingState);
        cmdList.bindSet(cullingBindingSet, 0);
        cmdList.setNamedUniform("numInputDrawables", numInputDrawables);
        cmdList.dispatch(Extent3D(numInputDrawables, 1, 1), Extent3D(64, 1, 1));

        // It would be nice if we could do GPU readback from last frame's count buffer (on the other hand, we do have renderdoc for this)
        //ImGui::Text("Issued draw calls: %i", numDrawCallsIssued);
    };
}
