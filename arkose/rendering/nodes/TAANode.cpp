#include "TAANode.h"

#include "rendering/RenderPipeline.h"
#include "scene/camera/Camera.h"
#include <imgui.h>

TAANode::TAANode(Camera& camera)
{
    if (m_taaEnabled) {
        camera.setFrustumJitteringEnabled(true);
    }
}

void TAANode::drawGui()
{
    ImGui::Checkbox("Enabled", &m_taaEnabled);

    if (ImGui::TreeNode("Advanced")) {
        ImGui::SliderFloat("Hysteresis", &m_hysteresis, 0.0f, 1.0f);
        ImGui::Checkbox("Use Catmull-Rom history sampling", &m_useCatmullRom);
        ImGui::TreePop();
    }
}

RenderPipelineNode::ExecuteCallback TAANode::construct(GpuScene& scene, Registry& reg)
{
    // Add LDR fallback for if placed after tone-mapping?
    Texture& currentFrameTexture = *reg.getTexture("SceneColor");
    Texture& currentFrameVelocity = *reg.getTexture("SceneNormalVelocity");

    Texture& accumulationTexture = reg.createTexture2D(currentFrameTexture.extent(), Texture::Format::RGBA16F);
    Texture& historyTexture = reg.createTexture2D(currentFrameTexture.extent(), currentFrameTexture.format(),
                                                  Texture::Filters::linear(), Texture::Mipmap::None, ImageWrapModes::clampAllToEdge());

    BindingSet& taaBindingSet = reg.createBindingSet({ ShaderBinding::storageTexture(accumulationTexture, ShaderStage::Compute),
                                                       ShaderBinding::sampledTexture(currentFrameTexture, ShaderStage::Compute),
                                                       ShaderBinding::sampledTexture(currentFrameVelocity, ShaderStage::Compute),
                                                       ShaderBinding::sampledTexture(historyTexture, ShaderStage::Compute) });

    StateBindings taaStateBindings;
    taaStateBindings.at(0, taaBindingSet);

    Shader taaComputeShader = Shader::createCompute("taa/taa.comp");
    ComputeState& taaComputeState = reg.createComputeState(taaComputeShader, taaStateBindings);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        scene.camera().setFrustumJitteringEnabled(m_taaEnabled);

        const bool wasEnabledThisFrame = m_taaEnabled && !m_taaEnabledPreviousFrame;
        m_taaEnabledPreviousFrame = m_taaEnabled;

        if (!m_taaEnabled) {
            return;
        }

        // NOTE: Relative first frame includes first frame after e.g. screen resize and other pipline invalidating actions
        const bool firstFrame = appState.isRelativeFirstFrame() || wasEnabledThisFrame;

        if (firstFrame) {
            cmdList.copyTexture(currentFrameTexture, accumulationTexture);
            return;
        }

        // Grab a copy of the current state of the accumulation texture; this is our history for this frame and we overwrite/accumulate in the accumulation texture
        ARKOSE_ASSERT(accumulationTexture.extent() == historyTexture.extent());
        cmdList.copyTexture(accumulationTexture, historyTexture);

        cmdList.setComputeState(taaComputeState);

        cmdList.setNamedUniform("hysteresis", m_hysteresis);
        cmdList.setNamedUniform("useCatmullRom", m_useCatmullRom);

        cmdList.dispatch(currentFrameTexture.extent3D(), { 16, 16, 1 });

        // TODO: Noooo.. we don't want to have to do this :(
        // There might be some clever way to avoid all these copies.
        cmdList.copyTexture(accumulationTexture, currentFrameTexture);
    };
}
