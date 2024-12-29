#pragma once

#include "rendering/RenderPipelineNode.h"

class OutputNode final : public RenderPipelineNode {
public:
    OutputNode(std::string sourceTextureName);

    std::string name() const override { return "Output"; }
    void drawGui() override;

    ExecuteCallback construct(GpuScene&, Registry&) override;

    void setTonemapMethod(int method);
    void setPaperWhiteLuminance(float luminance);
    void setRenderFilmGrain(bool enabled) { m_addFilmGrain = enabled; }
    void setRenderVignette(bool enabled) { m_applyVignette = enabled; }

private:
    std::string m_sourceTextureName;

    int m_outputColorSpace;
    int m_tonemapMethod;

    float m_paperWhiteLuminance { 350.0f };

    bool m_addFilmGrain { true };
    float m_filmGrainScale { 2.4f };

    bool m_applyVignette { true };
    float m_vignetteIntensity { 0.18f };

    bool m_applyColorGrade { false };

    enum BlackBars {
        None,
        Cinematic,
        CameraSensorAspectRatio,
    };

    BlackBars m_blackBars { BlackBars::None };
    vec4 calculateBlackBarLimits(GpuScene const&) const;
};
