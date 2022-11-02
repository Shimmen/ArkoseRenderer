#pragma once

#include "rendering/RenderPipelineNode.h"
#include "RTData.h"
#include "rendering/GpuScene.h"
#include "utility/Extent.h"

class DDGINode final : public RenderPipelineNode {
public:

    std::string name() const override { return "DDGI"; }
    void drawGui() override;

    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    Texture& createProbeAtlas(Registry&, const std::string& name, const ProbeGrid&, const ClearColor&, Texture::Format, int probeTileSize, int tileSidePadding) const;

    // we can dynamically choose to do fewer samples or probes, but not more since it defines the fixed image size
    static constexpr int MaxNumProbeSamples { 512 };
    static constexpr int MaxNumProbeUpdates { 4096 };

    int m_raysPerProbeInt = 256;
    float m_hysteresisIrradiance { 0.98f };
    float m_hysteresisVisibility { 0.98f };

    float m_visibilitySharpness { 50.0f };

    int m_probeUpdatesPerFrame { 2048 };
    int m_probeUpdateIdx { 0 };

    bool m_computeProbeOffsets { true };
    bool m_applyProbeOffsets { true };

    bool m_useSceneAmbient { true };
    float m_injectedAmbientLx { 100.0f };
};
