#include "TAANode.h"

#include "SceneNode.h"
#include "utility/Logging.h"
#include "utility/Profiling.h"
#include <imgui.h>

TAANode::TAANode(Scene& scene)
    : m_scene(scene)
{
    if (m_taaEnabled) {
        m_scene.camera().setFrustumJitteringEnabled(true);
    }
}

void TAANode::constructNode(Registry& reg)
{
    m_accumulationTexture = &reg.createTexture2D(m_scene.mainViewportSize(), Texture::Format::RGBA8);
}

RenderPipelineNode::ExecuteCallback TAANode::constructFrame(Registry& reg) const
{
    Texture& currentFrameTexture = *reg.getTexture("SceneColorLDR");
    Texture& currentFrameVelocity = *reg.getTexture("SceneVelocity");

    Texture& historyTexture = reg.createTexture2D(m_accumulationTexture->extent(), m_accumulationTexture->format(),
                                                  Texture::Filters::linear(), Texture::Mipmap::None, Texture::WrapModes::clampAllToEdge());

    BindingSet& taaBindingSet = reg.createBindingSet({ { 0, ShaderStageCompute, m_accumulationTexture, ShaderBindingType::StorageImage },
                                                       { 1, ShaderStageCompute, &currentFrameTexture, ShaderBindingType::TextureSampler },
                                                       { 2, ShaderStageCompute, &currentFrameVelocity, ShaderBindingType::TextureSampler },
                                                       { 3, ShaderStageCompute, &historyTexture, ShaderBindingType::TextureSampler } });

    Shader taaComputeShader = Shader::createCompute("taa/taa.comp");
    ComputeState& taaComputeState = reg.createComputeState(taaComputeShader, { &taaBindingSet });

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        ImGui::Checkbox("Enabled##taa", &m_taaEnabled);
        m_scene.camera().setFrustumJitteringEnabled(m_taaEnabled);

        static float hysteresis = 0.9f;
        static bool useCatmullRom = true;
        if (ImGui::TreeNode("Advanced##taa")) {
            ImGui::SliderFloat("Hysteresis", &hysteresis, 0.0f, 1.0f);
            ImGui::SliderFloat("Jitter scale", &m_scene.camera().frustumJitterScale, 0.0f, 1.0f);
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
            cmdList.copyTexture(currentFrameTexture, *m_accumulationTexture);
            return;
        }

        // Grab a copy of the current state of the accumulation texture; this is our history for this frame and we overwrite/accumulate in the accumulation texture
        ASSERT(m_accumulationTexture->extent() == historyTexture.extent());
        cmdList.copyTexture(*m_accumulationTexture, historyTexture);

        cmdList.setComputeState(taaComputeState);
        cmdList.bindSet(taaBindingSet, 0);

        cmdList.setNamedUniform("hysteresis", hysteresis);
        cmdList.setNamedUniform("useCatmullRom", useCatmullRom);

        cmdList.dispatch(currentFrameTexture.extent3D(), { 16, 16, 1 });


        // TODO: Noooo.. we don't want to have to do this :(
        // There might be some clever way to avoid all these copies.
        cmdList.copyTexture(*m_accumulationTexture, currentFrameTexture);
    };
}
