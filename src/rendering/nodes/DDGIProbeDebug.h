#pragma once

#include "rendering/RenderPipelineNode.h"

class DDGIProbeDebug final : public RenderPipelineNode {
public:
    DDGIProbeDebug(Scene&);

    std::string name() const override { return "DDGI probe debug"; }

    void constructNode(Registry&) override;
    ExecuteCallback constructFrame(Registry&) const override;

private:
    Scene& m_scene;

    BindingSet* m_ddgiSamplingSet { nullptr };

    void setUpSphereRenderData(Registry&);
    Buffer* m_sphereVertexBuffer { nullptr };
    Buffer* m_sphereIndexBuffer { nullptr };
    uint32_t m_indexCount { 0u };
};
