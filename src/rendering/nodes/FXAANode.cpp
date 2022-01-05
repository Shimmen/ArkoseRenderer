#include "FXAANode.h"

#include "SceneNode.h"
#include "utility/Logging.h"
#include "utility/Profiling.h"
#include <imgui.h>

FXAANode::FXAANode(Scene& scene)
    : m_scene(scene)
{
}

RenderPipelineNode::ExecuteCallback FXAANode::constructFrame(Registry& reg) const
{
    SCOPED_PROFILE_ZONE();

    std::vector<vec2> fullScreenTriangle { { -1, -3 }, { -1, 1 }, { 3, 1 } };
    Buffer& vertexBuffer = reg.createBuffer(std::move(fullScreenTriangle), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal);
    VertexLayout vertexLayout = VertexLayout { VertexComponent::Position2F };

    Texture& ldrTexture = *reg.getTexture("SceneColorLDR");

    BindingSet& fxaaBindingSet = reg.createBindingSet({ { 0, ShaderStageFragment, &ldrTexture, ShaderBindingType::TextureSampler } });
    Shader fxaaShader = Shader::createBasicRasterize("fxaa/anti-alias.vert", "fxaa/anti-alias.frag");
    RenderStateBuilder fxaaStateBuilder { reg.windowRenderTarget(), fxaaShader, vertexLayout };
    fxaaStateBuilder.stateBindings().at(0, fxaaBindingSet);
    fxaaStateBuilder.writeDepth = false;
    fxaaStateBuilder.testDepth = false;
    RenderState& fxaaRenderState = reg.createRenderState(fxaaStateBuilder);

    return [&](const AppState& appState, CommandList& cmdList) {

        static float subpix = 0.75f;
        static float edgeThreshold = 0.166f;
        static float edgeThresholdMin = 0.0833f;
        if (ImGui::TreeNode("Advanced")) {
            ImGui::SliderFloat("Sub-pixel AA", &subpix, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Edge threshold", &edgeThreshold, 0.063f, 0.333f, "%.3f");
            ImGui::SliderFloat("Edge threshold min", &edgeThresholdMin, 0.0312f, 0.0833f, "%.4f");
            ImGui::TreePop();
        }

        cmdList.beginRendering(fxaaRenderState, ClearColor::black(), 1.0f);
        {
            vec2 pixelSize = vec2(1.0f / ldrTexture.extent().width(), 1.0f / ldrTexture.extent().height());
            cmdList.setNamedUniform("fxaaQualityRcpFrame", pixelSize);

            cmdList.setNamedUniform("fxaaQualitySubpix", subpix);
            cmdList.setNamedUniform("fxaaQualityEdgeThreshold", edgeThreshold);
            cmdList.setNamedUniform("fxaaQualityEdgeThresholdMin", edgeThresholdMin);

            cmdList.setNamedUniform("filmGrainGain", m_scene.filmGrainGain());
            cmdList.setNamedUniform("frameIndex", appState.frameIndex());

            cmdList.draw(vertexBuffer, 3);
        }
        cmdList.endRendering();
    };
}
