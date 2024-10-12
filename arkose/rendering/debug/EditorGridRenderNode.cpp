#include "EditorGridRenderNode.h"

#include "core/Logging.h"
#include "rendering/GpuScene.h"

RenderPipelineNode::ExecuteCallback EditorGridRenderNode::construct(GpuScene& scene, Registry& reg)
{
    BindingSet& cameraBindingSet = *reg.getBindingSet("SceneCameraSet");

    // If placed after tonemapping, render to the post-tonemapped (LDR) target, otherwise fallback onto the HDR scene color
    Texture* sceneColorTex = reg.getTexture("SceneColorLDR");
    if (!sceneColorTex) {
        sceneColorTex = reg.getTexture("SceneColor");
    }

    Texture* sceneDepthTex = reg.getTexture("SceneDepth");

    std::vector<RenderTarget::Attachment> attachments;
    attachments.push_back({ RenderTarget::AttachmentType::Color0, sceneColorTex, LoadOp::Load, StoreOp::Store, RenderTargetBlendMode::AlphaBlending });
    if (sceneDepthTex->extent() == sceneColorTex->extent()) {
        attachments.push_back({ RenderTarget::AttachmentType::Depth, sceneDepthTex, LoadOp::Load, StoreOp::Store });
    } else {
        ARKOSE_LOG(Error, "EDITOR GRID UPSCALING HACK: Since the editor grid needs to depth test it can't use the non-upscaled "
                          "depth texture. For now, when using upscaling, we will simply not do any depth testing. This can be fixed "
                          "by copying the depth over to an upscaled texture (nearest sampling) and using that instead.");
    }

    RenderTarget& alphaBlendingRenderTarget = reg.createRenderTarget(attachments);

    Shader gridShader = Shader::createBasicRasterize("debug/grid.vert", "debug/grid.frag");

    RenderStateBuilder gridStateBuilder { alphaBlendingRenderTarget, gridShader, {} };
    gridStateBuilder.stateBindings().at(0, cameraBindingSet);
    gridStateBuilder.primitiveType = PrimitiveType::Triangles;
    gridStateBuilder.cullBackfaces = false;
    gridStateBuilder.writeDepth = false;
    gridStateBuilder.testDepth = true;

    RenderState& gridRenderState = reg.createRenderState(gridStateBuilder);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {
        cmdList.beginRendering(gridRenderState);
        cmdList.draw(6);
    };
}
