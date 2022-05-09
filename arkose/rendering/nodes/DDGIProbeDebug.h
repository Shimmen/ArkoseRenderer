#pragma once

#include "rendering/RenderPipelineNode.h"

class DDGIProbeDebug final : public RenderPipelineNode {
public:

    std::string name() const override { return "DDGI probe debug"; }
    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    void setUpSphereRenderData(GpuScene&, Registry&);
    Buffer* m_sphereVertexBuffer { nullptr };
    Buffer* m_sphereIndexBuffer { nullptr };
    uint32_t m_indexCount { 0u };
};
