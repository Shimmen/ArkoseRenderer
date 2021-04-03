#include "AutoExposureNode.h"

#include "CameraState.h"
#include "utility/Profiling.h"
#include <imgui.h>
#include <moos/vector.h>

AutoExposureNode::AutoExposureNode(Scene& scene)
    : RenderGraphNode(AutoExposureNode::name())
    , m_scene(scene)
{
}

void AutoExposureNode::exposureGUI(FpsCamera& camera) const
{
    auto& useAutoExposure = camera.useAutomaticExposure;
    if (ImGui::RadioButton("Automatic exposure", useAutoExposure))
        useAutoExposure = true;
    if (ImGui::RadioButton("Manual exposure", !useAutoExposure))
        useAutoExposure = false;

    ImGui::Spacing();
    ImGui::Spacing();

    if (useAutoExposure)
        automaticExposureGUI(camera);
    else
        manualExposureGUI(camera);
}

void AutoExposureNode::manualExposureGUI(FpsCamera& camera) const
{
    // Aperture
    {
        constexpr float steps[] = { 1.4f, 2.0f, 2.8f, 4.0f, 5.6f, 8.0f, 11.0f, 16.0f };
        constexpr int stepCount = sizeof(steps) / sizeof(steps[0]);
        constexpr float apertureMin = steps[0];
        constexpr float apertureMax = steps[stepCount - 1];

        ImGui::Text("Aperture f/%.1f", camera.aperture);

        // A kind of snapping SliderFloat implementation
        {
            ImGui::SliderFloat("aperture", &camera.aperture, apertureMin, apertureMax, "");

            int index = 1;
            for (; index < stepCount && camera.aperture >= steps[index]; ++index) { }
            float distUp = std::abs(steps[index] - camera.aperture);
            float distDown = std::abs(steps[index - 1] - camera.aperture);
            if (distDown < distUp)
                index -= 1;

            camera.aperture = steps[index];
        }
    }

    // Shutter speed
    {
        const int denominators[] = { 1000, 500, 400, 250, 125, 60, 30, 15, 8, 4, 2, 1 };
        const int denominatorCount = sizeof(denominators) / sizeof(denominators[0]);

        // Find the current value, snapped to the denominators
        int index = 1;
        {
            for (; index < denominatorCount && camera.shutterSpeed >= (1.0f / denominators[index]); ++index) { }
            float distUp = std::abs(1.0f / denominators[index] - camera.shutterSpeed);
            float distDown = std::abs(1.0f / denominators[index - 1] - camera.shutterSpeed);
            if (distDown < distUp)
                index -= 1;
        }

        ImGui::Text("Shutter speed  1/%i s", denominators[index]);
        ImGui::SliderInt("shutter", &index, 0, denominatorCount - 1, "");

        camera.shutterSpeed = 1.0f / denominators[index];
    }

    // ISO
    {
        int isoHundreds = int(camera.iso + 0.5f) / 100;

        ImGui::Text("ISO %i", 100 * isoHundreds);
        ImGui::SliderInt("ISO", &isoHundreds, 1, 64, "");

        camera.iso = float(isoHundreds * 100.0f);
    }
}

void AutoExposureNode::automaticExposureGUI(FpsCamera& camera) const
{
    ImGui::Text("Adaption rate", &camera.adaptionRate);
    ImGui::SliderFloat("", &camera.adaptionRate, 0.0001f, 2.0f, "%.4f", 5.0f);

    ImGui::Text("Exposure Compensation", &camera.exposureCompensation);
    ImGui::SliderFloat("ECs", &camera.exposureCompensation, -5.0f, +5.0f, "%.1f");
}

RenderGraphNode::ExecuteCallback AutoExposureNode::constructFrame(Registry& reg) const
{
    SCOPED_PROFILE_ZONE();

    Texture& logLuminanceTexture = reg.createTexture2D({ 512, 512 }, Texture::Format::R32F, Texture::Filters::linear(), Texture::Mipmap::Nearest);

    Texture& targetImage = *reg.getTexture("forward", "color").value();
    BindingSet& logLumBindingSet = reg.createBindingSet({ { 0, ShaderStageCompute, &targetImage, ShaderBindingType::TextureSampler },
                                                          { 1, ShaderStageCompute, &logLuminanceTexture, ShaderBindingType::StorageImage } });
    ComputeState& logLumComputeState = reg.createComputeState(Shader::createCompute("post/logLuminance.comp"), { &logLumBindingSet });

    Buffer& passDataBuffer = reg.createBuffer(2 * sizeof(float), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::TransferOptimal);
    passDataBuffer.setName("ExposurePassData");

    BindingSet& sourceDataBindingSet = reg.createBindingSet({ { 0, ShaderStageCompute, reg.getBuffer("scene", "camera") },
                                                              { 1, ShaderStageCompute, &logLuminanceTexture, ShaderBindingType::TextureSampler } });
    BindingSet& targetDataBindingSet = reg.createBindingSet({ { 0, ShaderStageCompute, &passDataBuffer } });
    ComputeState& exposeComputeState = reg.createComputeState(Shader::createCompute("post/expose.comp"), { &sourceDataBindingSet, &targetDataBindingSet });

    return [&](const AppState& appState, CommandList& cmdList) {
        FpsCamera& camera = m_scene.camera();
        exposureGUI(camera);

        if (!camera.useAutomaticExposure)
            return;

        // Calculate log-luminance over the whole image
        cmdList.setComputeState(logLumComputeState);
        cmdList.bindSet(logLumBindingSet, 0);
        cmdList.setNamedUniform("targetSize", logLuminanceTexture.extent());
        cmdList.dispatch(logLuminanceTexture.extent(), { 16, 16, 1 });

        // Compute average log-luminance by creating mipmaps
        cmdList.generateMipmaps(logLuminanceTexture);

        // FIXME: Don't use hardcoded event index! Maybe we should have some event resource type?
        static bool firstTimeAround  = true;
        cmdList.waitEvent(1, firstTimeAround ? PipelineStage::Host : PipelineStage::Compute);
        firstTimeAround = false;
        cmdList.resetEvent(1, PipelineStage::Compute);
        {
            cmdList.setComputeState(exposeComputeState);
            cmdList.bindSet(sourceDataBindingSet, 0);
            cmdList.bindSet(targetDataBindingSet, 1);
            BindingSet& prevData = m_lastFrameBindingSet.has_value()
                ? *m_lastFrameBindingSet.value()
                : targetDataBindingSet;
            cmdList.bindSet(prevData, 2);

            cmdList.setNamedUniform("deltaTime", (float)appState.deltaTime());
            cmdList.setNamedUniform("adaptionRate", appState.isRelativeFirstFrame() ? 9999.99f : camera.adaptionRate);

            cmdList.dispatch(1, 1, 1);
        }
        cmdList.signalEvent(1, PipelineStage::Compute);

        m_lastFrameBindingSet = &targetDataBindingSet;
        m_scene.setNextFrameExposureResultBuffer(passDataBuffer);
    };
}
