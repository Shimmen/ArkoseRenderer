#pragma once

#include "core/Types.h"
#include "rendering/RenderPipelineNode.h"
#include <ark/vector.h>

// Screen-space subsurface scattering
class SSSSNode final : public RenderPipelineNode {
public:
    std::string name() const override { return "Subsurface scattering"; }
    void drawGui() override;

    ExecuteCallback construct(GpuScene&, Registry&) override;

    void setEnabled(bool enabled) { m_enabled = enabled; }

private:
    bool m_enabled { true };

    struct Sample {
        vec2 point;
        float radius;
        float rcpPdf;
    };

    static void sampleBurleyDiffusionProfile(float u, float rcpS, float& outR, float& outRcpPdf);
    std::vector<Sample> generateDiffusionProfileSamples(u32 numSamples) const;

    static constexpr u32 MinSampleCount = 4;
    static constexpr u32 MaxSampleCount = 128;
    u32 m_sampleCount { 64 };

    // Importance sample based on the red component, as it's the most significant for skin.
    // For at least caucasian skin an sRGB value of 0.3 in the red channel is a pretty good default.
    float m_volumeAlbedoForImportanceSampling { 0.3f };

    bool m_samplesNeedUpload { true };
    std::vector<Sample> m_samples {};

    ////////////////////////////////////////////////////////////////////////////
    // Reference implementations
    // See https://www.desmos.com/calculator/wwazc2nfzq for graphed formulas

    // The original Burley diffusio profile
    float burleyDiffusion(float volumeAlbedo, float shape, float radius) const;

    // Function for deriving the shape parameter from volume albedo for the normalized variants below
    float calculateShapeValueForVolumeAlbedo(float volumeAlbedo) const;

    // Normalized variants depending only on the shape parameter directly
    float burleyNormalizedDiffusion(float shape, float radius) const;
    float burleyNormalizedDiffusionPDF(float shape, float radius) const;
    float burleyNormalizedDiffusionCDF(float shape, float radius) const;

};
