#include "CullingNode.h"

#include "SceneNode.h"
#include "geometry/Frustum.h"
#include "utility/Logging.h"
#include "utility/Profiling.h"
#include <imgui.h>

// Shared shader headers
using uint = uint32_t;
#include "IndirectData.h"
#include "LightData.h"
#include "ProbeGridData.h"

std::string CullingNode::name()
{
    return "culling";
}

CullingNode::CullingNode(Scene& scene)
    : RenderGraphNode(CullingNode::name())
    , m_scene(scene)
{
}

RenderGraphNode::ExecuteCallback CullingNode::constructFrame(Registry& reg) const
{
    SCOPED_PROFILE_ZONE();

    // todo: maybe default to smaller, and definitely actually grow when needed!
    static constexpr size_t initialBufferCount = 1024;

    Buffer& frustumPlaneBuffer = reg.createBuffer(6 * sizeof(vec4), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::TransferOptimal);
    Buffer& indirectDrawableBuffer = reg.createBuffer(initialBufferCount * sizeof(IndirectShaderDrawable), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::TransferOptimal);

    Buffer& drawableBuffer = reg.createBuffer(initialBufferCount * sizeof(ShaderDrawable), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOnly);
    drawableBuffer.setName("CulledDrawables");
    BindingSet& drawableBindingSet = reg.createBindingSet({ { 0, ShaderStageVertex, &drawableBuffer } });
    reg.publish("culled-drawables", drawableBindingSet);

    Buffer& indirectCmdBuffer = reg.createBuffer(initialBufferCount * sizeof(IndexedDrawCmd), Buffer::Usage::IndirectBuffer, Buffer::MemoryHint::GpuOnly);
    reg.publish("indirect-cmd-buffer", indirectCmdBuffer);

    Buffer& indirectCountBuffer = reg.createBuffer(sizeof(uint), Buffer::Usage::IndirectBuffer, Buffer::MemoryHint::TransferOptimal);
    reg.publish("indirect-count-buffer", indirectCountBuffer);

    BindingSet& cullingBindingSet = reg.createBindingSet({ { 0, ShaderStageCompute, &frustumPlaneBuffer },
                                                           { 1, ShaderStageCompute, &indirectDrawableBuffer },
                                                           { 2, ShaderStageCompute, &drawableBuffer },
                                                           { 3, ShaderStageCompute, &indirectCmdBuffer },
                                                           { 4, ShaderStageCompute, &indirectCountBuffer } });

    ComputeState& cullingState = reg.createComputeState(Shader::createCompute("culling/culling.comp"), { &cullingBindingSet });
    cullingState.setName("ForwardCulling");

    return [&](const AppState& appState, CommandList& cmdList) {

        mat4 cameraViewProjection = m_scene.camera().projectionMatrix() * m_scene.camera().viewMatrix();
        auto cameraFrustum = geometry::Frustum::createFromProjectionMatrix(cameraViewProjection);
        size_t planesByteSize;
        const geometry::Plane* planesData = cameraFrustum.rawPlaneData(&planesByteSize);
        ASSERT(planesByteSize == frustumPlaneBuffer.size());
        frustumPlaneBuffer.updateData(planesData, planesByteSize);

        std::vector<IndirectShaderDrawable> indirectDrawableData {};
        int numInputDrawables = m_scene.forEachMesh([&](size_t, Mesh& mesh) {
            DrawCallDescription drawCall = mesh.drawCallDescription({ VertexComponent::Position3F }, m_scene);
            indirectDrawableData.push_back({ .drawable = { .worldFromLocal = mesh.transform().worldMatrix(),
                                                           .worldFromTangent = mat4(mesh.transform().worldNormalMatrix()),
                                                           .materialIndex = mesh.materialIndex().value_or(0) },
                                             .localBoundingSphere = vec4(mesh.boundingSphere().center(), mesh.boundingSphere().radius()),
                                             .indexCount = drawCall.indexCount,
                                             .firstIndex = drawCall.firstIndex,
                                             .vertexOffset = drawCall.vertexOffset });
        });
        size_t newSize = numInputDrawables * sizeof(IndirectShaderDrawable);
        ASSERT(newSize <= indirectDrawableBuffer.size()); // fixme: grow instead of failing!
        indirectDrawableBuffer.updateData(indirectDrawableData.data(), newSize);

        uint32_t zero = 0u;
        indirectCountBuffer.updateData(&zero, sizeof(zero));

        cmdList.setComputeState(cullingState);
        cmdList.bindSet(cullingBindingSet, 0);
        cmdList.setNamedUniform("numInputDrawables", numInputDrawables);
        cmdList.dispatch(Extent3D(numInputDrawables, 1, 1), Extent3D(64, 1, 1));

        cmdList.bufferWriteBarrier({ &drawableBuffer, &indirectCmdBuffer, &indirectCountBuffer });

        // It would be nice if we could do GPU readback from last frame's count buffer
        //ImGui::Text("Issued draw calls: %i", numDrawCallsIssued);
    };
}
