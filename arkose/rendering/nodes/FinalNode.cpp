#include "FinalNode.h"

#include "core/Logging.h"
#include "utility/Profiling.h"
#include <imgui.h>

FinalNode::FinalNode(std::string sourceTextureName)
    : m_sourceTextureName(std::move(sourceTextureName))
{
}

RenderPipelineNode::ExecuteCallback FinalNode::construct(GpuScene& scene, Registry& reg)
{
    Texture* sourceTexture = reg.getTexture(m_sourceTextureName);
    if (!sourceTexture)
        ARKOSE_LOG(Fatal, "Final: specified source texture '{}' not found, exiting.", m_sourceTextureName);

    Texture& filmGrainTexture = *reg.getTexture("BlueNoise");
    BindingSet& bindingSet = reg.createBindingSet({ ShaderBinding::sampledTexture(*sourceTexture, ShaderStage::Fragment),
                                                    ShaderBinding::sampledTexture(filmGrainTexture, ShaderStage::Fragment) });

    std::vector<vec2> fullScreenTriangle { { -1, -3 }, { -1, 1 }, { 3, 1 } };
    Buffer& vertexBuffer = reg.createBuffer(std::move(fullScreenTriangle), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal);

    Shader taaShader = Shader::createBasicRasterize("final/final.vert", "final/postprocessing.frag");
    RenderStateBuilder stateBuilder { reg.windowRenderTarget(), taaShader, VertexLayout { VertexComponent::Position2F } };
    stateBuilder.stateBindings().at(0, bindingSet);
    stateBuilder.writeDepth = false;
    stateBuilder.testDepth = false;
    RenderState& renderState = reg.createRenderState(stateBuilder);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        ImGui::Checkbox("Add film grain", &m_addFilmGrain);
        ImGui::SliderFloat("Film grain scale", &m_filmGrainScale, 1.0f, 10.0f);
        float filmGrainGain = m_addFilmGrain ? scene.scene().filmGrainGain() : 0.0f;

        ImGui::Checkbox("Apply vignette", &m_applyVignette);
        ImGui::SliderFloat("Vignette intensity", &m_vignetteIntensity, 0.0f, 10.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
        float vignetteIntensity = m_applyVignette ? m_vignetteIntensity : 0.0f;

        cmdList.beginRendering(renderState, ClearColor::black(), 1.0f);
        {
            cmdList.setNamedUniform("filmGrainGain", filmGrainGain);
            cmdList.setNamedUniform("filmGrainScale", m_filmGrainScale);
            cmdList.setNamedUniform("filmGrainArrayIdx", appState.frameIndex() % filmGrainTexture.arrayCount());

            cmdList.setNamedUniform("vignetteIntensity", vignetteIntensity);
            cmdList.setNamedUniform("aspectRatio", scene.camera().aspectRatio());
        }
        cmdList.draw(vertexBuffer, 3);
        cmdList.endRendering();
    };
}
