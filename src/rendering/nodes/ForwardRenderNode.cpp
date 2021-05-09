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

    BindingSet& drawableBindingSet = *reg.getBindingSet("culling", "culled-drawables");
    BindingSet& materialBindingSet = m_scene.globalMaterialBindingSet();
    BindingSet& cameraBindingSet = *reg.getBindingSet("scene", "cameraSet");
    BindingSet& lightBindingSet = *reg.getBindingSet("scene", "lightSet");

    Texture& colorTexture = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA16F);
    colorTexture.setName("ForwardColor");
    reg.publish("color", colorTexture);

    Texture& gBufferDepthTexture = *reg.getTexture("g-buffer", "depth").value();
    auto depthAttachment = reg.hasPreviousNode("prepass")
        ? RenderTarget::Attachment { RenderTarget::AttachmentType::Depth, &gBufferDepthTexture, LoadOp::Load, StoreOp::Store }
        : RenderTarget::Attachment { RenderTarget::AttachmentType::Depth, &gBufferDepthTexture, LoadOp::Clear, StoreOp::Store };

    RenderTarget& renderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, &colorTexture },
                                                          { RenderTarget::AttachmentType::Color1, reg.getTexture("g-buffer", "normal").value() },
                                                          { RenderTarget::AttachmentType::Color2, reg.getTexture("g-buffer", "baseColor").value() },
                                                          depthAttachment });

    Shader shader = Shader::createBasicRasterize("forward/forward.vert", "forward/forward.frag");
    RenderStateBuilder renderStateBuilder { renderTarget, shader, m_vertexLayout };
    renderStateBuilder.depthCompare = DepthCompareOp::LessThanEqual;
    renderStateBuilder.stencilMode = StencilMode::AlwaysWrite;
    renderStateBuilder.addBindingSet(materialBindingSet);
    renderStateBuilder.addBindingSet(cameraBindingSet);
    renderStateBuilder.addBindingSet(drawableBindingSet);
    renderStateBuilder.addBindingSet(lightBindingSet);
    if (m_indirectLightBindingSet)
        renderStateBuilder.addBindingSet(*m_indirectLightBindingSet);
    RenderState& renderState = reg.createRenderState(renderStateBuilder);
    renderState.setName("ForwardOpaque");

    return [&](const AppState& appState, CommandList& cmdList) {

        cmdList.beginDebugLabel("Opaque");
        {
            m_scene.forEachMesh([&](size_t, Mesh& mesh) {
                mesh.ensureDrawCallIsAvailable(m_vertexLayout, m_scene);
            });

            cmdList.beginRendering(renderState, ClearColor::srgbColor(0, 0, 0, 0), 1.0f);
            cmdList.setNamedUniform("ambientAmount", m_scene.exposedAmbient());
            cmdList.setNamedUniform("indirectExposure", m_scene.lightPreExposureValue());

            cmdList.bindSet(cameraBindingSet, 0);
            cmdList.bindSet(materialBindingSet, 1);
            cmdList.bindSet(lightBindingSet, 2);
            cmdList.bindSet(drawableBindingSet, 4);
            if (m_indirectLightBindingSet)
                cmdList.bindSet(*m_indirectLightBindingSet, 3);

            cmdList.bindVertexBuffer(m_scene.globalVertexBufferForLayout(m_vertexLayout));
            cmdList.bindIndexBuffer(m_scene.globalIndexBuffer(), m_scene.globalIndexBufferType());

            cmdList.drawIndirect(*reg.getBuffer("culling", "indirect-cmd-buffer"), *reg.getBuffer("culling", "indirect-count-buffer"));

            cmdList.endRendering();
        }
        cmdList.endDebugLabel();

        cmdList.beginDebugLabel("Translucent");
        // TODO: Maybe put this in it's own node?
        cmdList.endDebugLabel();
    };
}
