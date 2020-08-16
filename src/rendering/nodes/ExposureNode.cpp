#include "ExposureNode.h"

#include "CameraState.h"
#include <imgui.h>
#include <mooslib/vector.h>

ExposureNode::ExposureNode(Scene& scene)
    : RenderGraphNode(ExposureNode::name())
    , m_scene(scene)
{
}

void ExposureNode::exposureGUI(FpsCamera& camera) const
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

void ExposureNode::manualExposureGUI(FpsCamera& camera) const
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

void ExposureNode::automaticExposureGUI(FpsCamera& camera) const
{
    ImGui::Text("Adaption rate", &camera.adaptionRate);
    ImGui::SliderFloat("", &camera.adaptionRate, 0.0001f, 2.0f, "%.4f", 5.0f);

    ImGui::Text("Exposure Compensation", &camera.exposureCompensation);
    ImGui::SliderFloat("ECs", &camera.exposureCompensation, -5.0f, +5.0f, "%.1f");
}

RenderGraphNode::ExecuteCallback ExposureNode::constructFrame(Registry& reg) const
{
    // Stores the current luminance for the image before exposure
    const Extent2D logLuminanceSize = { 1024, 1024 };
    Texture& logLuminanceTexture = reg.createTexture2D(logLuminanceSize, Texture::Format::R32F, Texture::Mipmap::Nearest);

    // Stores the last average luminance, after exposure, so we can do soft exposure transitions
    // TODO: Maybe use a storage buffer instead? Not sure what is faster for this.. note that we need read & write capabilities
    Texture& lastAvgLuminanceTexture = reg.createTexture2D({ 1, 1 }, Texture::Format::R32F);

    // TODO: Maybe we should generalize the concept of the "main image where we accululate light etc." so we don't need to refer to "forward"?
    Texture& targetImage = *reg.getTexture("forward", "color").value();

    BindingSet& logLumBindingSet = reg.createBindingSet({ { 0, ShaderStageCompute, &targetImage, ShaderBindingType::TextureSampler },
                                                          { 1, ShaderStageCompute, &logLuminanceTexture, ShaderBindingType::StorageImage } });
    ComputeState& logLumComputeState = reg.createComputeState(Shader::createCompute("post/logLuminance.comp"), { &logLumBindingSet });

    BindingSet& exposeBindingSet = reg.createBindingSet({ { 0, ShaderStageCompute, reg.getBuffer("scene", "camera") },
                                                          { 1, ShaderStageCompute, &logLuminanceTexture, ShaderBindingType::TextureSampler },
                                                          { 2, ShaderStageCompute, &targetImage, ShaderBindingType::StorageImage },
                                                          { 3, ShaderStageCompute, &lastAvgLuminanceTexture, ShaderBindingType::StorageImage } });
    ComputeState& exposeComputeState = reg.createComputeState(Shader::createCompute("post/expose.comp"), { &exposeBindingSet });

    return [&](const AppState& appState, CommandList& cmdList) {
        FpsCamera& camera = m_scene.camera();
        exposureGUI(camera);

        // Calculate log-luminance over the whole image
        cmdList.setComputeState(logLumComputeState);
        cmdList.bindSet(logLumBindingSet, 0);
        cmdList.dispatch(logLuminanceTexture.extent(), { 16, 16, 1 });

        // Compute average log-luminance by creating mipmaps
        cmdList.generateMipmaps(logLuminanceTexture);

        // Perform the exposure pass
        cmdList.setComputeState(exposeComputeState);
        cmdList.bindSet(exposeBindingSet, 0);
        cmdList.pushConstant(ShaderStageCompute, (float)appState.deltaTime(), 0);
        cmdList.pushConstant(ShaderStageCompute, camera.adaptionRate, 1 * sizeof(float));
        cmdList.pushConstant(ShaderStageCompute, camera.useAutomaticExposure, 2 * sizeof(float));
        cmdList.dispatch(targetImage.extent(), { 16, 16, 1 });
    };
}
