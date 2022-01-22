#include "PrepassNode.h"

#include "SceneNode.h"
#include "utility/Logging.h"
#include "utility/Profiling.h"
#include <imgui.h>

PrepassNode::PrepassNode(Scene& scene)
    : m_scene(scene)
{
}

RenderPipelineNode::ExecuteCallback PrepassNode::construct(Registry& reg)
{
    Texture& depthTexture = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::Depth24Stencil8, Texture::Filters::nearest());
    reg.publish("SceneDepth", depthTexture);

    BindingSet& opaqueDrawableBindingSet = *reg.getBindingSet("MainViewCulledDrawablesOpaqueSet");

    Shader prepassShader = Shader::createVertexOnly("forward/prepass.vert");
    RenderTarget& prepassRenderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Depth, &depthTexture, LoadOp::Clear, StoreOp::Store } });
    RenderStateBuilder prepassRenderStateBuilder { prepassRenderTarget, prepassShader, m_prepassVertexLayout };
    prepassRenderStateBuilder.stencilMode = StencilMode::AlwaysWrite;
    prepassRenderStateBuilder.stateBindings().at(0, opaqueDrawableBindingSet);
    RenderState& prepassRenderState = reg.createRenderState(prepassRenderStateBuilder);
    prepassRenderState.setName("ForwardZPrepass");

    Buffer& indirectDrawCmdsBuffer = *reg.getBuffer("MainViewOpaqueDrawCmds");
    Buffer& indirectDrawCountBuffer = *reg.getBuffer("MainViewOpaqueDrawCount");

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {
        int numInputDrawables = m_scene.forEachMesh([&](size_t, Mesh& mesh) {
            mesh.ensureDrawCallIsAvailable(m_prepassVertexLayout, m_scene);
        });

        cmdList.beginRendering(prepassRenderState, ClearColor::srgbColor(0, 0, 0, 0), 1.0f);

        cmdList.setNamedUniform("depthOffset", 0.00005f);
        cmdList.setNamedUniform("projectionFromWorld", m_scene.camera().viewProjectionMatrix());

        cmdList.bindVertexBuffer(m_scene.globalVertexBufferForLayout(m_prepassVertexLayout));
        cmdList.bindIndexBuffer(m_scene.globalIndexBuffer(), m_scene.globalIndexBufferType());

        cmdList.drawIndirect(indirectDrawCmdsBuffer, indirectDrawCountBuffer);

        cmdList.endRendering();
    };
}
