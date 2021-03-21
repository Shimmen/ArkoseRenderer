#include "ForwardRenderNode.h"

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

std::string ForwardRenderNode::name()
{
    return "forward";
}

ForwardRenderNode::ForwardRenderNode(Scene& scene)
    : RenderGraphNode(ForwardRenderNode::name())
    , m_scene(scene)
{
}

void ForwardRenderNode::constructNode(Registry& reg)
{
    SCOPED_PROFILE_ZONE();

    Texture* irradianceProbeTex = reg.getTextureWithoutDependency("diffuse-gi", "irradianceProbes");
    Texture* distanceProbeTex = reg.getTextureWithoutDependency("diffuse-gi", "filteredDistanceProbes");

    if (m_scene.hasProbeGrid() && irradianceProbeTex && distanceProbeTex) {
        ProbeGridData probeGridData = m_scene.probeGrid().toProbeGridDataObject();
        Buffer& probeGridDataBuffer = reg.createBufferForData(probeGridData, Buffer::Usage::UniformBuffer, Buffer::MemoryHint::GpuOptimal);

        m_indirectLightBindingSet = &reg.createBindingSet({ { 0, ShaderStageFragment, &probeGridDataBuffer },
                                                            { 1, ShaderStageFragment, irradianceProbeTex, ShaderBindingType::TextureSampler },
                                                            { 2, ShaderStageFragment, distanceProbeTex, ShaderBindingType::TextureSampler } });
    }
}

RenderGraphNode::ExecuteCallback ForwardRenderNode::constructFrame(Registry& reg) const
{
    SCOPED_PROFILE_ZONE();

    // Culling related

    // todo: maybe default to smaller, and definitely actually grow when needed!
    static constexpr size_t initialBufferCount = 1024;

    Buffer& frustumPlaneBuffer = reg.createBuffer(6 * sizeof(vec4), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::TransferOptimal);
    Buffer& indirectDrawableBuffer = reg.createBuffer(initialBufferCount * sizeof(IndirectShaderDrawable), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::TransferOptimal);

    Buffer& drawableBuffer = reg.createBuffer(initialBufferCount * sizeof(ShaderDrawable), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOnly);
    Buffer& indirectCmdBuffer = reg.createBuffer(initialBufferCount * sizeof(IndexedDrawCmd), Buffer::Usage::IndirectBuffer, Buffer::MemoryHint::GpuOnly);
    Buffer& indirectCountBuffer = reg.createBuffer(sizeof(uint), Buffer::Usage::IndirectBuffer, Buffer::MemoryHint::TransferOptimal);

    BindingSet& cullingBindingSet = reg.createBindingSet({ { 0, ShaderStageCompute, &frustumPlaneBuffer },
                                                           { 1, ShaderStageCompute, &indirectDrawableBuffer },
                                                           { 2, ShaderStageCompute, &drawableBuffer },
                                                           { 3, ShaderStageCompute, &indirectCmdBuffer },
                                                           { 4, ShaderStageCompute, &indirectCountBuffer } });

    ComputeState& cullingState = reg.createComputeState(Shader::createCompute("culling/culling.comp"), { &cullingBindingSet });
    cullingState.setName("ForwardCulling");

    // Normal forward pass related

    Texture& colorTexture = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA16F);
    colorTexture.setName("ForwardColor");
    reg.publish("color", colorTexture);

    RenderTarget& renderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, &colorTexture },
                                                          { RenderTarget::AttachmentType::Color1, reg.getTexture("g-buffer", "normal").value() },
                                                          { RenderTarget::AttachmentType::Color2, reg.getTexture("g-buffer", "baseColor").value() },
                                                          { RenderTarget::AttachmentType::Depth, reg.getTexture("g-buffer", "depth").value() } });

    BindingSet& drawableBindingSet = reg.createBindingSet({ { 0, ShaderStageVertex, &drawableBuffer } });
    BindingSet& materialBindingSet = m_scene.globalMaterialBindingSet();
    BindingSet& cameraBindingSet = *reg.getBindingSet("scene", "cameraSet");
    BindingSet& lightBindingSet = *reg.getBindingSet("scene", "lightSet");

    Shader shader = Shader::createBasicRasterize("forward/forward.vert", "forward/forward.frag");
    RenderStateBuilder renderStateBuilder { renderTarget, shader, m_vertexLayout };
    renderStateBuilder.addBindingSet(materialBindingSet);
    renderStateBuilder.addBindingSet(cameraBindingSet);
    renderStateBuilder.addBindingSet(drawableBindingSet);
    renderStateBuilder.addBindingSet(lightBindingSet);
    if (m_indirectLightBindingSet)
        renderStateBuilder.addBindingSet(*m_indirectLightBindingSet);
    RenderState& renderState = reg.createRenderState(renderStateBuilder);
    renderState.setName("ForwardOpaque");

    return [&](const AppState& appState, CommandList& cmdList) {

        cmdList.beginDebugLabel("Culling & indirect setup");
        {
            mat4 cameraViewProjection = m_scene.camera().projectionMatrix() * m_scene.camera().viewMatrix();
            auto cameraFrustum = geometry::Frustum::createFromProjectionMatrix(cameraViewProjection);
            size_t planesByteSize;
            const geometry::Plane* planesData = cameraFrustum.rawPlaneData(&planesByteSize);
            ASSERT(planesByteSize == frustumPlaneBuffer.size());
            frustumPlaneBuffer.updateData(planesData, planesByteSize);

            std::vector<IndirectShaderDrawable> indirectDrawableData {};
            int numInputDrawables = m_scene.forEachMesh([&](size_t, Mesh& mesh) {
                DrawCallDescription drawCall = mesh.drawCallDescription(m_vertexLayout, m_scene);
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
        }
        cmdList.endDebugLabel();

        // Opaque forward
        cmdList.beginDebugLabel("Opaque");
        {
            cmdList.beginRendering(renderState, ClearColor(0, 0, 0, 0), 1.0f);
            cmdList.setNamedUniform("ambientAmount", m_scene.ambient());

            cmdList.bindSet(cameraBindingSet, 0);
            cmdList.bindSet(materialBindingSet, 1);
            cmdList.bindSet(lightBindingSet, 2);
            cmdList.bindSet(drawableBindingSet, 4);
            if (m_indirectLightBindingSet)
                cmdList.bindSet(*m_indirectLightBindingSet, 3);

            cmdList.bindVertexBuffer(m_scene.globalVertexBufferForLayout(m_vertexLayout));
            cmdList.bindIndexBuffer(m_scene.globalIndexBuffer(), m_scene.globalIndexBufferType());

            cmdList.drawIndirect(indirectCmdBuffer, indirectCountBuffer);


        }
        cmdList.endDebugLabel();

        // It would be nice if we could do GPU readback from last frame's count buffer
        //ImGui::Text("Issued draw calls: %i", numDrawCallsIssued);
    };
}
