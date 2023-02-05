#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/GpuScene.h"

class FinalNode final : public RenderPipelineNode {
public:
    explicit FinalNode(std::string sourceTextureName);

    std::string name() const override { return "Final"; }
    void drawGui() override;

    ExecuteCallback construct(GpuScene&, Registry&) override;

    void setRenderFilmGrain(bool enabled) { m_addFilmGrain = enabled; }
    void setRenderVignette(bool enabled) { m_applyVignette = enabled; }

private:
    std::string m_sourceTextureName;

    bool m_addFilmGrain { true };
    float m_filmGrainScale { 2.4f };

    bool m_applyVignette { true };
    float m_vignetteIntensity { 0.18f };
};
