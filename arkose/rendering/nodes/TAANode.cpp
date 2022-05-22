#include "TAANode.h"

#include "rendering/camera/Camera.h"
#include <imgui.h>

TAANode::TAANode(Camera& camera)
{
    if (m_taaEnabled) {
        camera.setFrustumJitteringEnabled(true);
    }
}

RenderPipelineNode::ExecuteCallback TAANode::construct(GpuScene& scene, Registry& reg)
{
    ///////////////////////
    // constructNode
    Texture& accumulationTexture = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA8);
    ///////////////////////

    Texture& currentFrameTexture = *reg.getTexture("SceneColorLDR");
    Texture& currentFrameVelocity = *reg.getTexture("SceneNormalVelocity");

    Texture& historyTexture = reg.createTexture2D(accumulationTexture.extent(), accumulationTexture.format(),
                                                  Texture::Filters::linear(), Texture::Mipmap::None, Texture::WrapModes::clampAllToEdge());

    BindingSet& taaBindingSet = reg.createBindingSet({ ShaderBinding::storageTexture(accumulationTexture, ShaderStage::Compute),
                                                       ShaderBinding::sampledTexture(currentFrameTexture, ShaderStage::Compute),
                                                       ShaderBinding::sampledTexture(currentFrameVelocity, ShaderStage::Compute),
                                                       ShaderBinding::sampledTexture(historyTexture, ShaderStage::Compute) });

    Shader taaComputeShader = Shader::createCompute("taa/taa.comp");
    ComputeState& taaComputeState = reg.createComputeState(taaComputeShader, { &taaBindingSet });

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        ImGui::Checkbox("Enabled##taa", &m_taaEnabled);
        scene.camera().setFrustumJitteringEnabled(m_taaEnabled);

        static float hysteresis = 0.95f;
        static bool useCatmullRom = true;
        if (ImGui::TreeNode("Advanced##taa")) {
            ImGui::SliderFloat("Hysteresis", &hysteresis, 0.0f, 1.0f);
            ImGui::Checkbox("Use Catmull-Rom history sampling", &useCatmullRom);
            ImGui::TreePop();
        }

        const bool wasEnabledThisFrame = m_taaEnabled && !m_taaEnabledPreviousFrame;
        m_taaEnabledPreviousFrame = m_taaEnabled;

        if (!m_taaEnabled)
            return;

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
        cmdList.bindSet(taaBindingSet, 0);

        cmdList.setNamedUniform("hysteresis", hysteresis);
        cmdList.setNamedUniform("useCatmullRom", useCatmullRom);

        cmdList.dispatch(currentFrameTexture.extent3D(), { 16, 16, 1 });


        // TODO: Noooo.. we don't want to have to do this :(
        // There might be some clever way to avoid all these copies.
        cmdList.copyTexture(accumulationTexture, currentFrameTexture);
    };
}
