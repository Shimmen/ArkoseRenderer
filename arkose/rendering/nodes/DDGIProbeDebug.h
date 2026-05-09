#pragma once

#include "rendering/RenderPipelineNode.h"

class DDGIProbeDebug final : public RenderPipelineNode {
public:

    std::string name() const override { return "DDGI probe debug"; }
    void drawGui() override;

    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    DrawCallDescription createSphereRenderData(GpuScene&, Registry&);

    Buffer* m_sphereVertexBuffer { nullptr };
    Buffer* m_sphereIndexBuffer { nullptr };
    DrawCallDescription m_sphereDrawCall {};

    // 0 => disabled
    int m_debugVisualisation { 0 };

    float m_probeScale { 0.05f };
    float m_distanceScale { 0.002f };
    bool m_useProbeOffset { true };
};
