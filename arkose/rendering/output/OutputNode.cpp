#include "OutputNode.h"

#include "core/Logging.h"
#include "rendering/GpuScene.h"
#include "rendering/RenderPipeline.h"
#include <imgui.h>

#include "shaders/shared/ColorSpaceData.h"
#include "shaders/shared/TonemapData.h"

OutputNode::OutputNode(std::string sourceTextureName)
    : m_sourceTextureName(sourceTextureName)
    , m_tonemapMethod(TONEMAP_METHOD_AGX)
{
}

void OutputNode::setTonemapMethod(int method)
{
    ARKOSE_ASSERT(method >= TONEMAP_METHOD_CLAMP && method <= TONEMAP_METHOD_KHRONOS_PBR_NEUTRAL);
    m_tonemapMethod = method;
}

void OutputNode::setPaperWhiteLuminance(float luminance)
{
    ARKOSE_ASSERT(m_outputColorSpace != COLOR_SPACE_SRGB_NONLINEAR);
    m_paperWhiteLuminance = luminance;
}

void OutputNode::drawGui()
{
    ImGui::Text("Output color space: %s", m_outputColorSpace == COLOR_SPACE_SRGB_NONLINEAR ? "sRGB" : "HDR10 ST2084 (PQ EOTF)");

    if (m_outputColorSpace == COLOR_SPACE_SRGB_NONLINEAR) {
        ImGui::Text("Tonemap method:");
        if (ImGui::RadioButton("Clamp", m_tonemapMethod == TONEMAP_METHOD_CLAMP)) {
            m_tonemapMethod = TONEMAP_METHOD_CLAMP;
        }
        if (ImGui::RadioButton("Reinhard", m_tonemapMethod == TONEMAP_METHOD_REINHARD)) {
            m_tonemapMethod = TONEMAP_METHOD_REINHARD;
        }
        if (ImGui::RadioButton("ACES", m_tonemapMethod == TONEMAP_METHOD_ACES)) {
            m_tonemapMethod = TONEMAP_METHOD_ACES;
        }
        if (ImGui::RadioButton("AgX", m_tonemapMethod == TONEMAP_METHOD_AGX)) {
            m_tonemapMethod = TONEMAP_METHOD_AGX;
        }
        if (ImGui::RadioButton("Khronos PBR Neutral", m_tonemapMethod == TONEMAP_METHOD_KHRONOS_PBR_NEUTRAL)) {
            m_tonemapMethod = TONEMAP_METHOD_KHRONOS_PBR_NEUTRAL;
        }
    }

    if (m_outputColorSpace == COLOR_SPACE_HDR10_ST2084) {
        ImGui::SliderFloat("Paper-white luminance", &m_paperWhiteLuminance, 100.0f, 1000.0f, "%.0f");
    }

    ImGui::Separator();

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

RenderPipelineNode::ExecuteCallback OutputNode::construct(GpuScene& scene, Registry& reg)
{
    Texture* sourceTexture = reg.getTexture(m_sourceTextureName);
    if (!sourceTexture) {
        ARKOSE_LOG(Fatal, "Output: specified source texture '{}' not found, exiting.\n", m_sourceTextureName);
    }

    Texture const& filmGrainTexture = *reg.getTexture("BlueNoise");
    Texture const& colorGradingLUT = scene.colorGradingLUT();

    BindingSet& outputBindingSet = reg.createBindingSet({ ShaderBinding::sampledTexture(*sourceTexture, ShaderStage::Fragment),
                                                          ShaderBinding::sampledTexture(filmGrainTexture, ShaderStage::Fragment),
                                                          ShaderBinding::sampledTexture(colorGradingLUT, ShaderStage::Fragment) });

    // TODO: We should probably use compute for this.. we don't require interpolation or any type of depth writing etc.
    std::vector<vec2> fullScreenTriangle { { -1, -3 }, { -1, 1 }, { 3, 1 } };
    Buffer& vertexBuffer = reg.createBuffer(std::move(fullScreenTriangle), Buffer::Usage::Vertex);
    VertexLayout vertexLayout = VertexLayout { VertexComponent::Position2F };

    RenderTarget& renderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, reg.outputTexture(), LoadOp::Discard, StoreOp::Store } });
    Shader tonemapShader = Shader::createBasicRasterize("output/output.vert", "output/output.frag");
    RenderStateBuilder stateBuilder { renderTarget, tonemapShader, vertexLayout };
    stateBuilder.stateBindings().at(0, outputBindingSet);
    stateBuilder.writeDepth = false;
    stateBuilder.testDepth = false;
    RenderState& outputRenderState = reg.createRenderState(stateBuilder);

    switch (scene.backend().swapchainTransferFunction()) {
    case Backend::SwapchainTransferFunction::sRGB_nonLinear:
        m_outputColorSpace = COLOR_SPACE_SRGB_NONLINEAR;
        break;
    case Backend::SwapchainTransferFunction::ST2084:
        m_outputColorSpace = COLOR_SPACE_HDR10_ST2084;
        m_tonemapMethod = 0; // not relevant
        break;
    }

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {
        cmdList.beginRendering(outputRenderState);

        cmdList.setNamedUniform<int>("outputColorSpace", m_outputColorSpace);
        cmdList.setNamedUniform<int>("tonemapMethod", m_tonemapMethod);

        // TODO: Maybe move this property to the camera or something else..
        cmdList.setNamedUniform("paperWhiteLm", m_paperWhiteLuminance);

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

        cmdList.bindVertexBuffer(vertexBuffer, outputRenderState.vertexLayout().packedVertexSize(), 0);
        cmdList.draw(3);

        cmdList.endRendering();
    };
}

vec4 OutputNode::calculateBlackBarLimits(GpuScene const& scene) const
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

    Extent2D outputResolution = scene.pipeline().outputResolution();
    float windowAspectRatio = scene.camera().aspectRatio();
    float relativeAspectRatio = barAspectRatio / windowAspectRatio;

    if (relativeAspectRatio > 1.0f) {
        // draw letterbox-style black bars
        float windowHeight = static_cast<float>(outputResolution.height());
        float innerViewHeight = windowHeight / relativeAspectRatio;
        float barHeight = (windowHeight - innerViewHeight) / 2.0f;
        limits.y = barHeight;
        limits.w = windowHeight - barHeight;
    } else if (relativeAspectRatio < 1.0f) {
        // draw left-right black bars
        float windowWidth = static_cast<float>(outputResolution.width());
        float innerViewWidth = windowWidth * relativeAspectRatio;
        float barWidth = (windowWidth - innerViewWidth) / 2.0f;
        limits.x = barWidth;
        limits.z = windowWidth - barWidth;
    }

    return limits;
}
