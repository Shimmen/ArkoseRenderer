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

ForwardRenderNode::ForwardRenderNode(Scene& scene)
    : m_scene(scene)
{
}

void ForwardRenderNode::constructNode(Registry& reg)
{
    SCOPED_PROFILE_ZONE();

    if (reg.hasPreviousNode("DDGI")) {
        m_ddgiSamplingBindingSet = reg.getBindingSet("DDGISamplingSet");
    }
}

RenderPipelineNode::ExecuteCallback ForwardRenderNode::constructFrame(Registry& reg) const
{
    SCOPED_PROFILE_ZONE();

    Texture& colorTexture = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA16F);
    reg.publish("SceneColor", colorTexture);

    Texture& normalTexture = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA16F);
    reg.publish("SceneNormal", normalTexture);

    Texture& velocityTexture = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA16F);
    reg.publish("SceneVelocity", velocityTexture);

    Texture& baseColorTexture = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA8);
    reg.publish("SceneBaseColor", baseColorTexture);

    Texture& diffueGiTexture = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA16F);
    reg.publish("DiffuseGI", diffueGiTexture);

    RenderTarget::Attachment depthAttachment;
    if (reg.hasPreviousNode("Prepass")) {
        depthAttachment = RenderTarget::Attachment { RenderTarget::AttachmentType::Depth, reg.getTexture("SceneDepth"), LoadOp::Load, StoreOp::Store };
    } else {
        Texture& depthTexture = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::Depth24Stencil8, Texture::Filters::nearest());
        reg.publish("SceneDepth", depthTexture);
        depthAttachment = RenderTarget::Attachment { RenderTarget::AttachmentType::Depth, &depthTexture, LoadOp::Clear, StoreOp::Store };
    }

    RenderTarget& renderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, &colorTexture },
                                                          { RenderTarget::AttachmentType::Color1, &normalTexture },
                                                          { RenderTarget::AttachmentType::Color2, &velocityTexture },
                                                          { RenderTarget::AttachmentType::Color3, &baseColorTexture },
                                                          { RenderTarget::AttachmentType::Color4, &diffueGiTexture },
                                                          depthAttachment });

    BindingSet& cameraBindingSet = *reg.getBindingSet("SceneCameraSet");
    BindingSet& materialBindingSet = m_scene.globalMaterialBindingSet();
    BindingSet& lightBindingSet = *reg.getBindingSet("SceneLightSet");
    BindingSet& drawableBindingSet = *reg.getBindingSet("MainViewCulledDrawablesSet");

    bool useDDGI = m_ddgiSamplingBindingSet != nullptr;
    auto includeDDGIDefine = ShaderDefine::makeBool("FORWARD_INCLUDE_DDGI", useDDGI);

    Shader shader = Shader::createBasicRasterize("forward/forward.vert", "forward/forward.frag", { includeDDGIDefine });
    RenderStateBuilder renderStateBuilder { renderTarget, shader, m_vertexLayout };
    renderStateBuilder.depthCompare = DepthCompareOp::LessThanEqual;
    renderStateBuilder.stencilMode = reg.hasPreviousNode("Prepass") ? StencilMode::PassIfNotZero : StencilMode::AlwaysWrite;
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

            cmdList.drawIndirect(*reg.getBuffer("MainViewIndirectDrawCmds"), *reg.getBuffer("MainViewIndirectDrawCount"));

            cmdList.endRendering();
        }
        cmdList.endDebugLabel();

        cmdList.beginDebugLabel("Translucent");
        // TODO: Maybe put this in it's own node?
        cmdList.endDebugLabel();
    };
}
