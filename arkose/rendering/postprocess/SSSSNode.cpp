#include "SSSSNode.h"

#include "core/math/Fibonacci.h"
#include "rendering/GpuScene.h"
#include "rendering/RenderPipeline.h"
#include <ark/random.h>
#include <imgui.h>
#include <implot.h>

#define SSSS_USE_RNG_SAMPLES() (0)

void SSSSNode::drawGui()
{
    ImGui::Checkbox("Enabled", &m_enabled);

    {
        float plotWidth = ImGui::GetContentRegionAvail().x;
        float plotHeight = plotWidth;

        // NOTE: Samples are in millimeters!
        ImPlot::SetNextAxesLimits(-10.0, +10.0, -10.0, +10.0, ImPlotCond_Always);

        if (ImPlot::BeginPlot("Sample visualization", ImVec2(plotWidth, plotHeight), ImPlotFlags_Crosshairs | ImPlotFlags_Equal | ImPlotFlags_NoLegend)) {
            ImPlot::PlotScatter<float>("Samples",
                                       &m_samples.data()->point.x,
                                       &m_samples.data()->point.y,
                                       static_cast<int>(m_samples.size()),
                                       ImPlotScatterFlags_None, 0, sizeof(Sample));
            ImPlot::EndPlot();
        }
    }

#if SSSS_USE_RNG_SAMPLES()
    if (ImGui::Button("Regenerate samples")) {
        m_samples = generateDiffusionProfileSamples(m_sampleCount);
        m_samplesNeedUpload = true;
    }

    ImGui::SameLine();
    bool sampleSliderDidChange = ImGui::SliderScalar("##SampleCountLabel", ImGuiDataType_U32, &m_sampleCount, &MinSampleCount, &MaxSampleCount, "%d samples");
#else
    bool sampleSliderDidChange = ImGui::SliderScalar("Sample count", ImGuiDataType_U32, &m_sampleCount, &MinSampleCount, &MaxSampleCount, "%d samples");
#endif

    bool albedoSliderDidChange = false; 
    if (ImGui::TreeNode("Advanced")) {
        albedoSliderDidChange = ImGui::SliderFloat("Albedo ref.", &m_volumeAlbedoForImportanceSampling, 0.01f, 1.0f);
        ImGui::TreePop();
    }

    if (albedoSliderDidChange || (sampleSliderDidChange && m_sampleCount != m_samples.size())) {
        m_samples = generateDiffusionProfileSamples(m_sampleCount);
        m_samplesNeedUpload = true;
    }
}

RenderPipelineNode::ExecuteCallback SSSSNode::construct(GpuScene& scene, Registry& reg)
{
    Buffer& samplesBuffer = reg.createBuffer(MaxSampleCount * sizeof(Sample), Buffer::Usage::ConstantBuffer);
    m_samples = generateDiffusionProfileSamples(m_sampleCount);

    Texture& diffuseIrradiance = *reg.getTexture("SceneDiffuseIrradiance");
    Texture& sceneDepth = *reg.getTexture("SceneDepth");
    Texture& sceneBaseColor = *reg.getTexture("SceneBaseColor");
    Buffer& sceneCameraBuffer = *reg.getBuffer("SceneCameraData");

    BindingSet& visibilityBufferSampleSet = *reg.getBindingSet("VisibilityBufferData");

    Texture& ssssTex = reg.createTexture2D(pipeline().renderResolution(), diffuseIrradiance.format());

    BindingSet& ssssBindingSet = reg.createBindingSet({ ShaderBinding::storageTexture(ssssTex, ShaderStage::Compute),
                                                        ShaderBinding::sampledTexture(diffuseIrradiance, ShaderStage::Compute),
                                                        ShaderBinding::sampledTexture(sceneDepth, ShaderStage::Compute),
                                                        ShaderBinding::sampledTexture(sceneBaseColor, ShaderStage::Compute),
                                                        ShaderBinding::constantBuffer(samplesBuffer, ShaderStage::Compute),
                                                        ShaderBinding::constantBuffer(sceneCameraBuffer, ShaderStage::Compute) });

    StateBindings ssssStateBindings;
    ssssStateBindings.at(0, ssssBindingSet);
    ssssStateBindings.at(1, visibilityBufferSampleSet);

    Shader ssssShader = Shader::createCompute("postprocess/ssss.comp", { ShaderDefine::makeInt("MAX_SAMPLE_COUNT", MaxSampleCount) });
    ComputeState& ssssState = reg.createComputeState(ssssShader, ssssStateBindings);

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

        Extent2D targetSize = pipeline().renderResolution();
        cmdList.setNamedUniform("targetSize", targetSize);
        cmdList.setNamedUniform("sampleCount", m_sampleCount);

        cmdList.dispatch(targetSize, { 8, 8, 1 });

        // TODO: Don't copy, instead use some smart system to point the next "SceneDiffuseIrradiance" to this
        cmdList.textureWriteBarrier(ssssTex);
        cmdList.copyTexture(ssssTex, diffuseIrradiance);
    };
}

float SSSSNode::burleyDiffusion(float volumeAlbedo, float shape, float radius) const
{
    float const& A = volumeAlbedo;
    float const& s = shape;
    float const& r = radius;

    return A * s * ((std::exp(-s * r) + std::exp(-s * r / 3.0f)) / (8.0f * ark::PI * r));
}

float SSSSNode::calculateShapeValueForVolumeAlbedo(float volumeAlbedo) const
{
    // Based on https://graphics.pixar.com/library/ApproxBSSRDF/approxbssrdfslides.pdf
    // Calculate the "shape" variable for the diffusion profile, using the "searchlight configuration" (see page 42)

    float const& A = volumeAlbedo;

    return 1.85f - A + 7.0f * std::pow(std::abs(A - 0.8f), 3.0f);
}

float SSSSNode::burleyNormalizedDiffusion(float shape, float radius) const
{
    float const& s = shape;
    float const& r = radius;

    return s * ((std::exp(-s * r) + std::exp(-s * r / 3.0f)) / (8.0f * ark::PI));
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

// From https://zero-radiance.github.io/post/sampling-diffusion/:
// Performs sampling of a Normalized Burley diffusion profile in polar coordinates.
// 'u' is the random number (the value of the CDF): [0, 1).
// rcp(s) = 1 / ShapeParam = ScatteringDistance.
// 'r' is the sampled radial distance, s.t. (u = 0 -> r = 0) and (u = 1 -> r = Inf).
// rcp(Pdf) is the reciprocal of the corresponding PDF value.
void SSSSNode::sampleBurleyDiffusionProfile(float u, float rcpS, float& outR, float& outRcpPdf)
{
    u = 1 - u; // Convert CDF to CCDF; the resulting value of (u != 0)

    float g = 1.0f + (4.0f * u) * (2.0f * u + std::sqrt(1.0f + (4.0f * u) * u));
    float n = std::exp2(std::log2(g) * (-1.0f / 3.0f));                 // g^(-1/3)
    float p = (g * n) * n;                                              // g^(+1/3)
    float c = 1.0f + p + n;                                             // 1 + g^(+1/3) + g^(-1/3)
    float x = (3.0f / ark::LOG2_E) * std::log2(c * ark::rcp(4.0f * u)); // 3 * Log[c / (4 * u)]

    // x      = s * r
    // exp_13 = Exp[-x/3] = Exp[-1/3 * 3 * Log[c / (4 * u)]]
    // exp_13 = Exp[-Log[c / (4 * u)]] = (4 * u) / c
    // exp_1  = Exp[-x] = exp_13 * exp_13 * exp_13
    // expSum = exp_1 + exp_13 = exp_13 * (1 + exp_13 * exp_13)
    // rcpExp = rcp(expSum) = c^3 / ((4 * u) * (c^2 + 16 * u^2))
    float rcpExp = ((c * c) * c) * ark::rcp((4.0f * u) * ((c * c) + (4.0f * u) * (4.0f * u)));

    outR      = x * rcpS;
    outRcpPdf = (8.0f * ark::PI * rcpS) * rcpExp; // (8 * Pi) / s / (Exp[-s * r / 3] + Exp[-s * r])
}

std::vector<SSSSNode::Sample> SSSSNode::generateDiffusionProfileSamples(u32 numSamples) const
{
    //
    // See "Efficient screen space subsurface scattering" from Unity at Siggraph 2018:
    // https://advances.realtimerendering.com/s2018/Efficient%20screen%20space%20subsurface%20scattering%20Siggraph%202018.pdf
    //

    float shapeRed = calculateShapeValueForVolumeAlbedo(m_volumeAlbedoForImportanceSampling);

    std::vector<Sample> samples {};
    samples.reserve(numSamples);

#if SSSS_USE_RNG_SAMPLES()
    ark::Random rng {};
#endif

    for (u32 sampleIdx = 0; sampleIdx < numSamples; ++sampleIdx) {

        vec2 latticePoint = geometry::fibonacciLattice(sampleIdx, numSamples);
        float angle = ark::TWO_PI * latticePoint.x;

        // NOTE: We can use either a rng or we just hardcode a nice fibonacci spiral
        // with constant steps. Hardcoding seems to be what most other solutions are
        // doing and it does produce a more reliably good result in the end..
#if SSSS_USE_RNG_SAMPLES()
        float u = rng.randomFloat();
#else
        float u = float(sampleIdx) / float(numSamples) + (0.5f / float(numSamples));
#endif

        float sampledRadius;
        float sampledRcpPdf;
        sampleBurleyDiffusionProfile(u, 1.0f / shapeRed, sampledRadius, sampledRcpPdf);

        vec2 cartesianPoint = vec2(std::cos(angle), std::sin(angle)) * sampledRadius;

        // Verify that the sampled PDF matches up with what we'd expect (just as a sanity check)
        float analyticalPdf = burleyNormalizedDiffusionPDF(shapeRed, sampledRadius);
        ARKOSE_ASSERT(std::abs((1.0f / analyticalPdf) - sampledRcpPdf) < 1e-2f);

        samples.push_back(Sample { .point = cartesianPoint,
                                   .radius = sampledRadius,
                                   .rcpPdf = sampledRcpPdf });
    }

    return samples;
}
