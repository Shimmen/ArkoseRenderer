#pragma once

#include "rendering/RenderPipelineNode.h"

class DDGIProbeDebug final : public RenderPipelineNode {
public:

    std::string name() const override { return "DDGI probe debug"; }
    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    DrawCallDescription createSphereRenderData(GpuScene&, Registry&);

    DrawCallDescription m_sphereDrawCall {};

    float m_probeScale { 0.05f };
    float m_distanceScale { 0.002f };
};
