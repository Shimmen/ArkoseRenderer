#include "ForwardRenderNode.h"

#include "LightData.h"
#include "ProbeGridData.h"
#include "SceneNode.h"
#include "geometry/Frustum.h"
#include "utility/Logging.h"
#include "utility/Profiling.h"
#include <imgui.h>

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

    Texture& colorTexture = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA16F);
    reg.publish("color", colorTexture);

    RenderTarget& renderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, &colorTexture },
                                                          { RenderTarget::AttachmentType::Color1, reg.getTexture("g-buffer", "normal").value() },
                                                          { RenderTarget::AttachmentType::Color2, reg.getTexture("g-buffer", "baseColor").value() },
                                                          { RenderTarget::AttachmentType::Depth, reg.getTexture("g-buffer", "depth").value() } });

    BindingSet& cameraBindingSet = *reg.getBindingSet("scene", "cameraSet");
    BindingSet& objectBindingSet = *reg.getBindingSet("scene", "objectSet");
    BindingSet& lightBindingSet = *reg.getBindingSet("scene", "lightSet");

    Shader shader = Shader::createBasicRasterize("forward/forward.vert", "forward/forward.frag");
    RenderStateBuilder renderStateBuilder { renderTarget, shader, vertexLayout };
    renderStateBuilder.addBindingSet(cameraBindingSet);
    renderStateBuilder.addBindingSet(objectBindingSet);
    renderStateBuilder.addBindingSet(lightBindingSet);
    if (m_indirectLightBindingSet)
        renderStateBuilder.addBindingSet(*m_indirectLightBindingSet);
    RenderState& renderState = reg.createRenderState(renderStateBuilder);

    m_scene.forEachMesh([&](size_t, Mesh& mesh) {
        mesh.ensureVertexBuffer(semanticVertexLayout);
        mesh.ensureIndexBuffer();
    });

    return [&](const AppState& appState, CommandList& cmdList) {
        cmdList.beginRendering(renderState, ClearColor(0, 0, 0, 0), 1.0f);
        cmdList.setNamedUniform("ambientAmount", m_scene.ambient());

        cmdList.bindSet(cameraBindingSet, 0);
        cmdList.bindSet(objectBindingSet, 1);
        cmdList.bindSet(lightBindingSet, 2);
        if (m_indirectLightBindingSet)
            cmdList.bindSet(*m_indirectLightBindingSet, 3);

        // Perform frustum culling & draw non-culled meshes

        int numDrawCallsIssued = 0;
        mat4 cameraViewProjection = m_scene.camera().projectionMatrix() * m_scene.camera().viewMatrix();
        auto cameraFrustum = geometry::Frustum::createFromProjectionMatrix(cameraViewProjection);

        m_scene.forEachMesh([&](size_t meshIndex, Mesh& mesh) {
            geometry::Sphere sphere = mesh.boundingSphere().transformed(mesh.transform().worldMatrix());
            if (!cameraFrustum.includesSphere(sphere))
                return;

            cmdList.drawIndexed(mesh.vertexBuffer(semanticVertexLayout),
                                mesh.indexBuffer(), mesh.indexCount(), mesh.indexType(),
                                meshIndex);
            numDrawCallsIssued += 1;
        });

        ImGui::Text("Issued draw calls: %i", numDrawCallsIssued);
    };
}
