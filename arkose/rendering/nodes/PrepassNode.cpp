#include "PrepassNode.h"

#include "rendering/util/ScopedDebugZone.h"
#include <imgui.h>

PrepassNode::PrepassNode(PrepassMode mode)
    : m_mode(mode)
{
}

RenderPipelineNode::ExecuteCallback PrepassNode::construct(GpuScene& scene, Registry& reg)
{
    RenderState& prepassOpaqueRenderState = makeRenderState(reg, scene, PassType::Opaque);
    Buffer& opaqueIndirectDrawCmdsBuffer = *reg.getBuffer("MainViewOpaqueDrawCmds");
    Buffer& opaqueIndirectDrawCountBuffer = *reg.getBuffer("MainViewOpaqueDrawCount");

    RenderState& prepassMaskedRenderState = makeRenderState(reg, scene, PassType::Masked);
    Buffer& maskedIndirectDrawCmdsBuffer = *reg.getBuffer("MainViewMaskedDrawCmds");
    Buffer& maskedIndirectDrawCountBuffer = *reg.getBuffer("MainViewMaskedDrawCount");

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        auto setCommonConstants = [&]() {
            cmdList.setNamedUniform("depthOffset", 0.00005f);
            cmdList.setNamedUniform("projectionFromWorld", scene.camera().viewProjectionMatrix());
        };

        if (m_mode == PrepassMode::OpaqueObjectsOnly || m_mode == PrepassMode::AllOpaquePixels) {
            ScopedDebugZone zone { cmdList, "Opaque" };

            VertexLayout opaqueVertexLayout = prepassOpaqueRenderState.vertexLayout();
            scene.ensureDrawCallIsAvailableForAll(opaqueVertexLayout);

            cmdList.beginRendering(prepassOpaqueRenderState, ClearValue::blackAtMaxDepth());
            setCommonConstants();

            cmdList.bindVertexBuffer(scene.globalVertexBufferForLayout(opaqueVertexLayout));
            cmdList.bindIndexBuffer(scene.globalIndexBuffer(), scene.globalIndexBufferType());

            cmdList.drawIndirect(opaqueIndirectDrawCmdsBuffer, opaqueIndirectDrawCountBuffer);

            cmdList.endRendering();
        }

        if (m_mode == PrepassMode::AllOpaquePixels) {
            ScopedDebugZone zone { cmdList, "Masked" };

            VertexLayout maskedVertexLayout = prepassMaskedRenderState.vertexLayout();
            scene.ensureDrawCallIsAvailableForAll(maskedVertexLayout);

            cmdList.beginRendering(prepassMaskedRenderState);
            setCommonConstants();

            cmdList.bindVertexBuffer(scene.globalVertexBufferForLayout(maskedVertexLayout));
            cmdList.bindIndexBuffer(scene.globalIndexBuffer(), scene.globalIndexBufferType());

            cmdList.drawIndirect(maskedIndirectDrawCmdsBuffer, maskedIndirectDrawCountBuffer);

            cmdList.endRendering();
        }
    };
}

RenderState& PrepassNode::makeRenderState(Registry& reg, GpuScene const& scene, PassType type) const
{
    Shader shader {};
    VertexLayout vertexLayout {};
    BindingSet* drawablesBindingSet = nullptr;
    const char* stateName = "";
    LoadOp loadOp = LoadOp::Clear;

    switch (type) {
    case PassType::Opaque:
        shader = Shader::createVertexOnly("forward/prepass.vert");
        vertexLayout = { VertexComponent::Position3F };
        drawablesBindingSet = reg.getBindingSet("MainViewCulledDrawablesOpaqueSet");
        stateName = "PrepassOpaque";
        loadOp = LoadOp::Clear;
        break;
    case PassType::Masked:
        shader = Shader::createBasicRasterize("forward/prepassMasked.vert", "forward/prepassMasked.frag");
        vertexLayout = { VertexComponent::Position3F, VertexComponent::TexCoord2F };
        drawablesBindingSet = reg.getBindingSet("MainViewCulledDrawablesMaskedSet");
        stateName = "PrepassMasked";
        loadOp = LoadOp::Load;
        break;
    }

    Texture* sceneDepth = reg.getTexture("SceneDepth");
    RenderTarget& prepassRenderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Depth, sceneDepth, loadOp, StoreOp::Store } });
    
    RenderStateBuilder renderStateBuilder { prepassRenderTarget, shader, vertexLayout };

    renderStateBuilder.depthCompare = DepthCompareOp::LessThanEqual;
    renderStateBuilder.stencilMode = StencilMode::AlwaysWrite;

    switch (type) {
    case PassType::Opaque:
        renderStateBuilder.stateBindings().at(0, *reg.getBindingSet("MainViewCulledDrawablesOpaqueSet"));
        break;
    case PassType::Masked:
        renderStateBuilder.stateBindings().at(0, *reg.getBindingSet("MainViewCulledDrawablesMaskedSet"));
        renderStateBuilder.stateBindings().at(1, scene.globalMaterialBindingSet());
        break;
    }

    RenderState& renderState = reg.createRenderState(renderStateBuilder);
    renderState.setName(stateName);

    return renderState;
}
