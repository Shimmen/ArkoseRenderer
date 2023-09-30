#include "SSSSNode.h"

#include "core/math/Fibonacci.h"
#include "rendering/GpuScene.h"
#include "rendering/RenderPipeline.h"
#include <imgui.h>
#include <implot.h>

void SSSSNode::drawGui()
{
    ImGui::Checkbox("Enabled", &m_enabled);

    {
        float plotWidth = ImGui::GetContentRegionAvail().x;
        float plotHeight = plotWidth;

        ImPlot::SetNextAxesLimits(-1.1, +1.1, -1.1, +1.1, ImPlotCond_Always);

        if (ImPlot::BeginPlot("Sample visualization", ImVec2(plotWidth, plotHeight), ImPlotFlags_Crosshairs | ImPlotFlags_Equal | ImPlotFlags_NoLegend)) {
            ImPlot::PlotScatter<float>("Samples",
                                       &m_samples.data()->point.x,
                                       &m_samples.data()->point.y,
                                       static_cast<int>(m_samples.size()),
                                       ImPlotScatterFlags_None, 0, sizeof(Sample));
            ImPlot::EndPlot();
        }
    }

    if (ImGui::Button("Regenerate samples")) {
        m_samples = generateDiffusionProfileSamples(m_sampleCount);
        m_samplesNeedUpload = true;
    }

    ImGui::SameLine();

    bool sampleSliderDidChange = ImGui::SliderScalar("##SampleCountLabel", ImGuiDataType_U32, &m_sampleCount, &MinSampleCount, &MaxSampleCount, "%d samples");

    if (sampleSliderDidChange && m_sampleCount != m_samples.size()) {
        m_samples = generateDiffusionProfileSamples(m_sampleCount);
        m_samplesNeedUpload = true;
    }
}

RenderPipelineNode::ExecuteCallback SSSSNode::construct(GpuScene& scene, Registry& reg)
{
    Buffer& samplesBuffer = reg.createBuffer(MaxSampleCount * sizeof(Sample), Buffer::Usage::ConstantBuffer, Buffer::MemoryHint::GpuOptimal);
    m_samples = generateDiffusionProfileSamples(m_sampleCount);

    Texture& sceneColor = *reg.getTexture("SceneColor");
    Texture& sceneDepth = *reg.getTexture("SceneDepth");
    Buffer& sceneCameraBuffer = *reg.getBuffer("SceneCameraData");

    BindingSet& visibilityBufferSampleSet = *reg.getBindingSet("VisibilityBufferData");
    BindingSet& materialBindingSet = scene.globalMaterialBindingSet();

    Texture& ssssTex = reg.createTexture2D(pipeline().renderResolution(), sceneColor.format());

    BindingSet& ssssBindingSet = reg.createBindingSet({ ShaderBinding::storageTexture(ssssTex, ShaderStage::Compute),
                                                        ShaderBinding::sampledTexture(sceneColor, ShaderStage::Compute),
                                                        ShaderBinding::sampledTexture(sceneDepth, ShaderStage::Compute),
                                                        ShaderBinding::constantBuffer(samplesBuffer, ShaderStage::Compute),
                                                        ShaderBinding::constantBuffer(sceneCameraBuffer, ShaderStage::Compute) });
    Shader ssssShader = Shader::createCompute("postprocess/ssss.comp", { ShaderDefine::makeInt("MAX_SAMPLE_COUNT", MaxSampleCount) });
    ComputeState& ssssState = reg.createComputeState(ssssShader, { &ssssBindingSet, &visibilityBufferSampleSet, &materialBindingSet });

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {
        if (!m_enabled) {
            return;
        }

        if (appState.isRelativeFirstFrame() || m_samplesNeedUpload) {
            uploadBuffer.upload(m_samples, samplesBuffer);
            cmdList.executeBufferCopyOperations(uploadBuffer.popPendingOperations());
            m_samplesNeedUpload = false;
        }

        cmdList.setComputeState(ssssState);
        cmdList.bindSet(ssssBindingSet, 0);
        cmdList.bindSet(visibilityBufferSampleSet, 1);
        cmdList.bindSet(materialBindingSet, 2);

        Extent2D targetSize = pipeline().renderResolution();
        cmdList.setNamedUniform("targetSize", targetSize);
        cmdList.setNamedUniform("sampleCount", m_sampleCount);

        cmdList.dispatch(targetSize, { 8, 8, 1 });

        // TODO: Don't copy, instead use some smart system to point the next "SceneColor" to this
        cmdList.textureWriteBarrier(ssssTex);
        cmdList.copyTexture(ssssTex, sceneColor);
    };
}

float SSSSNode::burleyNormalizedDiffusion(float volumeAlbedo, float shape, float radius) const
{
    float const& A = volumeAlbedo;
    float const& s = shape;
    float const& r = radius;

    return A * s * ((std::exp(-s * r) + std::exp(-s * r / 3.0f)) / (8.0f * ark::PI * r));
}

float SSSSNode::burleyNormalizedDiffusionPDF(float shape, float radius) const
{
    float const& s = shape;
    float const& r = radius;

    return (s / (8.0f * ark::PI)) * (std::exp(-s * r) + std::exp(-s * r / 3.0f));
}

float SSSSNode::burleyNormalizedDiffusionCDF(float shape, float radius) const
{
    float const& s = shape;
    float const& r = radius;

    return 1.0f - 0.25f * std::exp(-s * r) - 0.75f * std::exp(-s * r / 3.0f);
}


std::vector<SSSSNode::Sample> SSSSNode::generateDiffusionProfileSamples(u32 numSamples) const
{
    //
    // See "Efficient screen space subsurface scattering" from Unity at Siggraph 2018:
    // https://advances.realtimerendering.com/s2018/Efficient%20screen%20space%20subsurface%20scattering%20Siggraph%202018.pdf
    //

    // Importance sample based on the red component, as it's the most significant for skin
    constexpr float shapeRed = 0.3f;

    std::vector<Sample> samples {};
    samples.reserve(numSamples);

    float totalPdf = 0.0f;

    for (u32 sampleIdx = 0; sampleIdx < numSamples; ++sampleIdx) {

        vec2 angleRadius = geometry::fibonacciSpiral(sampleIdx, numSamples);
        float angle = angleRadius.x;
        float radius = angleRadius.y;

        // TODO: Importance sample radius! We want really tight around the origin! But also, accurate according to the distribution!
        radius = std::pow(radius, 17.0f);

        vec2 cartesianPoint = vec2(std::cos(angle), std::sin(angle)) * radius;

        // TODO: Sample a gausian matching the diffusion profile we're interested in!
        float pdf = burleyNormalizedDiffusionPDF(shapeRed, radius);
        totalPdf += pdf;

        samples.push_back(Sample { .point = cartesianPoint,
                                   .pdf = pdf });
    }

    // Normalize PDFs so the sum is 1
    for (Sample& sample : samples) {
        sample.pdf /= totalPdf;
    }

    return samples;
}
