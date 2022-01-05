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

RenderPipelineNode::ExecuteCallback TAANode::constructFrame(Registry& reg) const
{
    SCOPED_PROFILE_ZONE();

    // TODO: We should probably use compute for this now.. we don't require interpolation or any type of depth writing etc.
    std::vector<vec2> fullScreenTriangle { { -1, -3 }, { -1, 1 }, { 3, 1 } };
    Buffer& vertexBuffer = reg.createBuffer(std::move(fullScreenTriangle), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal);
    VertexLayout vertexLayout = VertexLayout { VertexComponent::Position2F };

    Texture& ldrTexture = *reg.getTexture("SceneColorLDR");

    BindingSet& taaBindingSet = reg.createBindingSet({ { 0, ShaderStageFragment, &ldrTexture, ShaderBindingType::TextureSampler } });
    Shader taaShader = Shader::createBasicRasterize("taa/taa.vert", "taa/taa.frag");
    RenderStateBuilder taaStateBuilder { reg.windowRenderTarget(), taaShader, vertexLayout };
    taaStateBuilder.stateBindings().at(0, taaBindingSet);
    taaStateBuilder.writeDepth = false;
    taaStateBuilder.testDepth = false;
    RenderState& taaRenderState = reg.createRenderState(taaStateBuilder);

    return [&](const AppState& appState, CommandList& cmdList) {

        // TODO: Respect enabled state!
        ImGui::Checkbox("Enabled##taa", &m_taaEnabled);
        m_scene.camera().setFrustumJitteringEnabled(m_taaEnabled);

        cmdList.beginRendering(taaRenderState, ClearColor::black(), 1.0f);
        {
            /*
            static float filmGrainGain = 0.035f;
            cmdList.pushConstant(ShaderStageFragment, filmGrainGain, sizeof(vec2) + 3 * sizeof(float));
            cmdList.pushConstant(ShaderStageFragment, appState.frameIndex(), sizeof(vec2) + 4 * sizeof(float));

            if (ImGui::TreeNode("Film grain")) {
                ImGui::SliderFloat("Grain gain", &filmGrainGain, 0.0f, 1.0f);
                ImGui::TreePop();
            }
            */
        }
        cmdList.draw(vertexBuffer, 3);
        cmdList.endRendering();
    };
}
