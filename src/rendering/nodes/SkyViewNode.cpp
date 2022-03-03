#include "SkyViewNode.h"

#include "utility/Profiling.h"
#include <imgui.h>

RenderPipelineNode::ExecuteCallback SkyViewNode::construct(Scene& scene, Registry& reg)
{
    Texture& sceneColor = *reg.getTexture("SceneColor");
    Texture& sceneNormalVelocity = *reg.getTexture("SceneNormalVelocity"); // todo: velocity shouldn't be strictly required as it is now!
    Texture& depthStencilImage = *reg.getTexture("SceneDepth");

    Texture& skyViewTexture = scene.environmentMap().empty()
        ? reg.createPixelTexture(vec4(1.0f), true)
        : reg.loadTexture2D(scene.environmentMap(), true, false);

    BindingSet& skyViewRasterizeBindingSet = reg.createBindingSet({ { 0, ShaderStage::AnyRasterize, reg.getBuffer("SceneCameraData") },
                                                                    { 1, ShaderStage::Fragment, &skyViewTexture, ShaderBindingType::TextureSampler } });

    RenderTarget& renderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, &sceneColor, LoadOp::Load, StoreOp::Store },
                                                          { RenderTarget::AttachmentType::Color1, &sceneNormalVelocity, LoadOp::Load, StoreOp::Store },
                                                          { RenderTarget::AttachmentType::Depth, &depthStencilImage, LoadOp::Load, StoreOp::Store } });

    Shader rasterizeShader = Shader::createBasicRasterize("sky-view/sky-view.vert", "sky-view/sky-view.frag");
    RenderStateBuilder renderStateBuilder { renderTarget, rasterizeShader, { VertexComponent::Position2F } };
    renderStateBuilder.testDepth = false;
    renderStateBuilder.writeDepth = false;
    renderStateBuilder.stencilMode = StencilMode::PassIfZero; // i.e. if no geometry is written to this pixel
    renderStateBuilder.stateBindings().at(0, skyViewRasterizeBindingSet);
    RenderState& skyViewRenderState = reg.createRenderState(renderStateBuilder);

    Buffer& fullscreenTriangleVertexBuffer = reg.createBuffer(std::vector<vec2> { { -1, -3 }, { -1, 1 }, { 3, 1 } },
                                                              Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        static bool skyViewEnabled = true;
        if (ImGui::RadioButton("Sky view enabled", skyViewEnabled))
            skyViewEnabled = true;
        if (ImGui::RadioButton("Velocity only", !skyViewEnabled))
            skyViewEnabled = false;

        float envMultiplier = skyViewEnabled ? scene.exposedEnvironmentMultiplier() : 0.0f;

        cmdList.beginRendering(skyViewRenderState);
        cmdList.setNamedUniform("environmentMultiplier", envMultiplier);
        cmdList.setNamedUniform("frustumJitterCorrection", scene.camera().frustumJitterUVCorrection());
        cmdList.draw(fullscreenTriangleVertexBuffer, 3);
        cmdList.endRendering();
    };
}
