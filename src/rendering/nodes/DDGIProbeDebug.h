#pragma once

#include "rendering/RenderPipelineNode.h"

class DDGIProbeDebug final : public RenderPipelineNode {
public:
    DDGIProbeDebug(Scene&);

    std::string name() const override { return "DDGI probe debug"; }

    ExecuteCallback construct(Registry&) override;

private:
    Scene& m_scene;

    void setUpSphereRenderData(Registry&);
    Buffer* m_sphereVertexBuffer { nullptr };
    Buffer* m_sphereIndexBuffer { nullptr };
    uint32_t m_indexCount { 0u };
};
