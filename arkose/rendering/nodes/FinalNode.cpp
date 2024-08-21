#include "FinalNode.h"

#include "core/Logging.h"
#include "utility/Profiling.h"
#include <imgui.h>

FinalNode::FinalNode(std::string sourceTextureName)
    : m_sourceTextureName(std::move(sourceTextureName))
{
}

void FinalNode::drawGui()
{
    ImGui::Checkbox("Add film grain", &m_addFilmGrain);
    ImGui::SliderFloat("Film grain scale", &m_filmGrainScale, 1.0f, 10.0f);

    ImGui::Checkbox("Apply vignette", &m_applyVignette);
    ImGui::SliderFloat("Vignette intensity", &m_vignetteIntensity, 0.0f, 10.0f, "%.2f", ImGuiSliderFlags_Logarithmic);

    ImGui::Checkbox("Apply color grade", &m_applyColorGrade);

    auto nameForBlackBars = [](BlackBars blackBars) -> char const* {
        switch (blackBars) {
        case BlackBars::None:
            return "None";
        case BlackBars::Cinematic:
            return "Cinematic";
        case BlackBars::CameraSensorAspectRatio:
            return "Virtual camera sensor aspect ratio";
        default:
            ASSERT_NOT_REACHED();
        }
    };

    if (ImGui::BeginCombo("Black bars", nameForBlackBars(m_blackBars))) {
        if (ImGui::Selectable(nameForBlackBars(BlackBars::None), m_blackBars == BlackBars::None)) {
            m_blackBars = BlackBars::None;
        }
        if (ImGui::Selectable(nameForBlackBars(BlackBars::Cinematic), m_blackBars == BlackBars::Cinematic)) {
            m_blackBars = BlackBars::Cinematic;
        }
        if (ImGui::Selectable(nameForBlackBars(BlackBars::CameraSensorAspectRatio), m_blackBars == BlackBars::CameraSensorAspectRatio)) {
            m_blackBars = BlackBars::CameraSensorAspectRatio;
        }
        ImGui::EndCombo();
    }
}

RenderPipelineNode::ExecuteCallback FinalNode::construct(GpuScene& scene, Registry& reg)
{
    Texture* sourceTexture = reg.getTexture(m_sourceTextureName);
    if (!sourceTexture) {
        ARKOSE_LOG(Fatal, "Final: specified source texture '{}' not found, exiting.", m_sourceTextureName);
    }

    Texture const& filmGrainTexture = *reg.getTexture("BlueNoise");
    Texture const& colorGradingLUT = scene.colorGradingLUT();

    BindingSet& bindingSet = reg.createBindingSet({ ShaderBinding::sampledTexture(*sourceTexture, ShaderStage::Fragment),
                                                    ShaderBinding::sampledTexture(filmGrainTexture, ShaderStage::Fragment),
                                                    ShaderBinding::sampledTexture(colorGradingLUT, ShaderStage::Fragment) });

    std::vector<vec2> fullScreenTriangle { { -1, -3 }, { -1, 1 }, { 3, 1 } };
    Buffer& vertexBuffer = reg.createBuffer(std::move(fullScreenTriangle), Buffer::Usage::Vertex);

    Shader taaShader = Shader::createBasicRasterize("final/final.vert", "final/postprocessing.frag");
    RenderStateBuilder stateBuilder { reg.windowRenderTarget(), taaShader, VertexLayout { VertexComponent::Position2F } };
    stateBuilder.stateBindings().at(0, bindingSet);
    stateBuilder.writeDepth = false;
    stateBuilder.testDepth = false;
    RenderState& renderState = reg.createRenderState(stateBuilder);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        cmdList.beginRendering(renderState, ClearValue::blackAtMaxDepth());
        {
            float filmGrainGain = m_addFilmGrain ? scene.camera().filmGrainGain() : 0.0f;
            cmdList.setNamedUniform("filmGrainGain", filmGrainGain);
            cmdList.setNamedUniform("filmGrainScale", m_filmGrainScale);
            cmdList.setNamedUniform("filmGrainArrayIdx", appState.frameIndex() % filmGrainTexture.arrayCount());

            float vignetteIntensity = m_applyVignette ? m_vignetteIntensity : 0.0f;
            cmdList.setNamedUniform("vignetteIntensity", vignetteIntensity);
            cmdList.setNamedUniform("aspectRatio", scene.camera().aspectRatio());

            vec4 blackBarsLimits = calculateBlackBarLimits(scene);
            cmdList.setNamedUniform("blackBarsLimits", blackBarsLimits);

            cmdList.setNamedUniform("colorGrade", m_applyColorGrade);
        }

        cmdList.bindVertexBuffer(vertexBuffer, renderState.vertexLayout().packedVertexSize(), 0);
        cmdList.draw(3);

        cmdList.endRendering();
    };
}

vec4 FinalNode::calculateBlackBarLimits(GpuScene const& scene) const
{
    // default/null limits
    vec4 limits = vec4(0.0f, 0.0f, std::numeric_limits<float>::max(), std::numeric_limits<float>::max());

    if (m_blackBars == BlackBars::None) {
        return limits;
    }

    float barAspectRatio = 1.0f;
    switch (m_blackBars) {
    case BlackBars::Cinematic:
        barAspectRatio = 2.39f / 1.0f;
        break;
    case BlackBars::CameraSensorAspectRatio:
        barAspectRatio = scene.camera().sensorVirtualAspectRatio();
        break;
    default:
        ASSERT_NOT_REACHED();
    }

    float windowAspectRatio = scene.camera().aspectRatio();
    float relativeAspectRatio = barAspectRatio / windowAspectRatio;

    if (relativeAspectRatio > 1.0f) {
        // draw letterbox-style black bars
        float windowHeight = static_cast<float>(scene.camera().viewport().height());
        float innerViewHeight = windowHeight / relativeAspectRatio;
        float barHeight = (windowHeight - innerViewHeight) / 2.0f;
        limits.y = barHeight;
        limits.w = windowHeight - barHeight;
    } else if (relativeAspectRatio < 1.0f) {
        // draw left-right black bars
        float windowWidth = static_cast<float>(scene.camera().viewport().width());
        float innerViewWidth = windowWidth * relativeAspectRatio;
        float barWidth = (windowWidth - innerViewWidth) / 2.0f;
        limits.x = barWidth;
        limits.z = windowWidth - barWidth;
    }

    return limits;
}
