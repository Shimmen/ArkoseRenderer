#include "SkyViewNode.h"

#include "utility/Profiling.h"
#include <imgui.h>

void SkyViewNode::drawGui()
{
    if (ImGui::RadioButton("Sky view enabled", m_skyViewEnabled)) {
        m_skyViewEnabled = true;
    }
    if (ImGui::RadioButton("Velocity only", !m_skyViewEnabled)) {
        m_skyViewEnabled = false;
    }
}

RenderPipelineNode::ExecuteCallback SkyViewNode::construct(GpuScene& scene, Registry& reg)
{
    Texture& sceneColor = *reg.getTexture("SceneColor");
    Texture& sceneNormalVelocity = *reg.getTexture("SceneNormalVelocity"); // todo: velocity shouldn't be strictly required as it is now!
    Texture& depthStencilImage = *reg.getTexture("SceneDepth");

    BindingSet& skyViewRasterizeBindingSet = reg.createBindingSet({ ShaderBinding::constantBuffer(*reg.getBuffer("SceneCameraData"), ShaderStage::AnyRasterize),
                                                                    ShaderBinding::sampledTexture(scene.environmentMapTexture(), ShaderStage::Fragment) });

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

        float envMultiplier = m_skyViewEnabled ? scene.preExposedEnvironmentBrightnessFactor() : 0.0f;

        cmdList.beginRendering(skyViewRenderState);
        cmdList.setNamedUniform("environmentMultiplier", envMultiplier);
        cmdList.setNamedUniform("frustumJitterCorrection", scene.camera().frustumJitterUVCorrection());
        cmdList.draw(fullscreenTriangleVertexBuffer, 3);
        cmdList.endRendering();
    };
}
