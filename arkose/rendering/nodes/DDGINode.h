#pragma once

#include "rendering/RenderPipelineNode.h"
#include "RTData.h"
#include "rendering/GpuScene.h"
#include "utility/Extent.h"

class DDGINode final : public RenderPipelineNode {
public:

    static constexpr int MaxNumProbeUpdatesPerFrame = 4096;
    static constexpr int RaysPerProbe = 128;

    std::string name() const override { return "DDGI"; }
    void drawGui() override;

    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    Texture& createProbeAtlas(Registry&, const std::string& name, const ProbeGrid&, const ClearColor&, Texture::Format, int probeTileSize, int tileSidePadding) const;

    // we can dynamically choose to do fewer samples but not more since it defines the fixed image size
    static constexpr int MaxNumProbeSamples { 128 };

    int m_raysPerProbeInt = MaxNumProbeSamples;
    float m_hysteresisIrradiance { 0.98f };
    float m_hysteresisVisibility { 0.98f };

    // TODO: What's a good default?!
    float m_visibilitySharpness { 5.0f };

    bool m_useSceneAmbient { true };
    float m_injectedAmbientLx { 100.0f };
};
