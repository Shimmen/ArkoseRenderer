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

    if (reg.hasPreviousNode("ddgi")) {
        m_ddgiSamplingBindingSet = reg.getBindingSet("ddgi-sampling-set");
    }
}

RenderGraphNode::ExecuteCallback ForwardRenderNode::constructFrame(Registry& reg) const
{
    SCOPED_PROFILE_ZONE();

    BindingSet& drawableBindingSet = *reg.getBindingSet("culling-culled-drawables");
    BindingSet& materialBindingSet = m_scene.globalMaterialBindingSet();
    BindingSet& cameraBindingSet = *reg.getBindingSet("cameraSet");
    BindingSet& lightBindingSet = *reg.getBindingSet("lightSet");

    Texture& colorTexture = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA16F);
    colorTexture.setName("SceneColor");
    reg.publish("SceneColor", colorTexture);

    Texture& diffueGiTexture = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA16F);
    diffueGiTexture.setName("DiffuseGI");
    reg.publish("DiffuseGI", diffueGiTexture);

    Texture& gBufferDepthTexture = *reg.getTexture("SceneDepth");
    auto depthAttachment = reg.hasPreviousNode("prepass")
        ? RenderTarget::Attachment { RenderTarget::AttachmentType::Depth, &gBufferDepthTexture, LoadOp::Load, StoreOp::Store }
        : RenderTarget::Attachment { RenderTarget::AttachmentType::Depth, &gBufferDepthTexture, LoadOp::Clear, StoreOp::Store };

    RenderTarget& renderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, &colorTexture },
                                                          { RenderTarget::AttachmentType::Color1, reg.getTexture("SceneNormal") },
                                                          { RenderTarget::AttachmentType::Color2, reg.getTexture("SceneBaseColor") },
                                                          { RenderTarget::AttachmentType::Color3, &diffueGiTexture },
                                                          depthAttachment });

    bool useDDGI = m_ddgiSamplingBindingSet != nullptr;
    auto includeDDGIDefine = ShaderDefine::makeBool("FORWARD_INCLUDE_DDGI", useDDGI);

    Shader shader = Shader::createBasicRasterize("forward/forward.vert", "forward/forward.frag", { includeDDGIDefine });
    RenderStateBuilder renderStateBuilder { renderTarget, shader, m_vertexLayout };
    renderStateBuilder.depthCompare = DepthCompareOp::LessThanEqual;
    renderStateBuilder.stencilMode = reg.hasPreviousNode("prepass") ? StencilMode::PassIfNotZero : StencilMode::AlwaysWrite;
    renderStateBuilder.stateBindings().at(0, cameraBindingSet);
    renderStateBuilder.stateBindings().at(1, materialBindingSet);
    renderStateBuilder.stateBindings().at(2, lightBindingSet);
    renderStateBuilder.stateBindings().at(3, drawableBindingSet);
    if (useDDGI)
        renderStateBuilder.stateBindings().at(5, *m_ddgiSamplingBindingSet);
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

            cmdList.bindVertexBuffer(m_scene.globalVertexBufferForLayout(m_vertexLayout));
            cmdList.bindIndexBuffer(m_scene.globalIndexBuffer(), m_scene.globalIndexBufferType());

            cmdList.drawIndirect(*reg.getBuffer("culling-indirect-cmd-buffer"), *reg.getBuffer("culling-indirect-count-buffer"));

            cmdList.endRendering();
        }
        cmdList.endDebugLabel();

        cmdList.beginDebugLabel("Translucent");
        // TODO: Maybe put this in it's own node?
        cmdList.endDebugLabel();
    };
}
