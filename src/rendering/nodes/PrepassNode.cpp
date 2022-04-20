#include "PrepassNode.h"

#include <imgui.h>

RenderPipelineNode::ExecuteCallback PrepassNode::construct(GpuScene& scene, Registry& reg)
{
    Texture* sceneDepth = reg.getTexture("SceneDepth");
    BindingSet& opaqueDrawableBindingSet = *reg.getBindingSet("MainViewCulledDrawablesOpaqueSet");

    Shader prepassShader = Shader::createVertexOnly("forward/prepass.vert");
    RenderTarget& prepassRenderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Depth, sceneDepth, LoadOp::Clear, StoreOp::Store } });
    RenderStateBuilder prepassRenderStateBuilder { prepassRenderTarget, prepassShader, m_prepassVertexLayout };
    prepassRenderStateBuilder.stencilMode = StencilMode::AlwaysWrite;
    prepassRenderStateBuilder.stateBindings().at(0, opaqueDrawableBindingSet);
    RenderState& prepassRenderState = reg.createRenderState(prepassRenderStateBuilder);
    prepassRenderState.setName("ForwardZPrepass");

    Buffer& indirectDrawCmdsBuffer = *reg.getBuffer("MainViewOpaqueDrawCmds");
    Buffer& indirectDrawCountBuffer = *reg.getBuffer("MainViewOpaqueDrawCount");

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {
        scene.forEachMesh([&](size_t, Mesh& mesh) {
            mesh.ensureDrawCallIsAvailable(m_prepassVertexLayout, scene);
        });

        cmdList.beginRendering(prepassRenderState, ClearColor::srgbColor(0, 0, 0, 0), 1.0f);

        cmdList.setNamedUniform("depthOffset", 0.00005f);
        cmdList.setNamedUniform("projectionFromWorld", scene.camera().viewProjectionMatrix());

        cmdList.bindVertexBuffer(scene.globalVertexBufferForLayout(m_prepassVertexLayout));
        cmdList.bindIndexBuffer(scene.globalIndexBuffer(), scene.globalIndexBufferType());

        cmdList.drawIndirect(indirectDrawCmdsBuffer, indirectDrawCountBuffer);

        cmdList.endRendering();
    };
}
