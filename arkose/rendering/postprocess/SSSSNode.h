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

    // See https://www.desmos.com/calculator/hgshdxuuik for graphed formulas
    float burleyNormalizedDiffusion(float volumeAlbedo, float shape, float radius) const;
    float burleyNormalizedDiffusionPDF(float shape, float radius) const;
    float burleyNormalizedDiffusionCDF(float shape, float radius) const;

    float m_indexOfRefraction { 1.4f }; // 1.4 is pretty good for skin

    struct Sample {
        vec2 point;
        float pdf;
        float padding;
    };
    std::vector<Sample> generateDiffusionProfileSamples(u32 numSamples) const;

    static constexpr u32 MinSampleCount = 4;
    static constexpr u32 MaxSampleCount = 128;
    u32 m_sampleCount { 64 };

    bool m_samplesNeedUpload { true };
    std::vector<Sample> m_samples {};
};
