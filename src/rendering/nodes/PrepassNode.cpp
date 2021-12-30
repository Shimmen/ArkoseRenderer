#include "PrepassNode.h"

#include "SceneNode.h"
#include "geometry/Frustum.h"
#include "utility/Logging.h"
#include "utility/Profiling.h"
#include <imgui.h>

std::string PrepassNode::name()
{
    return "prepass";
}

PrepassNode::PrepassNode(Scene& scene)
    : RenderPipelineNode(PrepassNode::name())
    , m_scene(scene)
{
}

RenderPipelineNode::ExecuteCallback PrepassNode::constructFrame(Registry& reg) const
{
    SCOPED_PROFILE_ZONE();

    Texture& gBufferDepthTexture = *reg.getTexture("SceneDepth");
    BindingSet& drawableBindingSet = *reg.getBindingSet("MainViewCulledDrawablesSet");

    Shader prepassShader = Shader::createVertexOnly("forward/prepass.vert");
    RenderTarget& prepassRenderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Depth, &gBufferDepthTexture, LoadOp::Clear, StoreOp::Store } });
    RenderStateBuilder prepassRenderStateBuilder { prepassRenderTarget, prepassShader, m_prepassVertexLayout };
    prepassRenderStateBuilder.stencilMode = StencilMode::AlwaysWrite;
    prepassRenderStateBuilder.stateBindings().at(0, drawableBindingSet);
    RenderState& prepassRenderState = reg.createRenderState(prepassRenderStateBuilder);
    prepassRenderState.setName("ForwardZPrepass");

    return [&](const AppState& appState, CommandList& cmdList) {
        int numInputDrawables = m_scene.forEachMesh([&](size_t, Mesh& mesh) {
            mesh.ensureDrawCallIsAvailable(m_prepassVertexLayout, m_scene);
        });

        cmdList.beginRendering(prepassRenderState, ClearColor::srgbColor(0, 0, 0, 0), 1.0f);

        cmdList.setNamedUniform("depthOffset", 0.00005f);
        cmdList.setNamedUniform("projectionFromWorld", m_scene.camera().viewProjectionMatrix());

        cmdList.bindVertexBuffer(m_scene.globalVertexBufferForLayout(m_prepassVertexLayout));
        cmdList.bindIndexBuffer(m_scene.globalIndexBuffer(), m_scene.globalIndexBufferType());
        cmdList.drawIndirect(*reg.getBuffer("MainViewIndirectDrawCmds"), *reg.getBuffer("MainViewIndirectDrawCount"));

        cmdList.endRendering();

        cmdList.textureWriteBarrier(gBufferDepthTexture);
    };
}
