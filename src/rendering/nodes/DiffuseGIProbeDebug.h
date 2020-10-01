#pragma once

#include "../RenderGraphNode.h"
#include "rendering/nodes/DiffuseGINode.h"

class DiffuseGIProbeDebug final : public RenderGraphNode {
public:
    DiffuseGIProbeDebug(Scene&, DiffuseGINode::ProbeGridDescription);

    static std::string name();
    std::optional<std::string> displayName() const override { return "Diffuse GI probe debug"; }

    void constructNode(Registry&) override;
    ExecuteCallback constructFrame(Registry&) const override;

private:
    Scene& m_scene;
    DiffuseGINode::ProbeGridDescription m_grid;

    Texture* m_probeData;

    void setUpSphereRenderData(Registry&);
    Buffer* m_sphereVertexBuffer { nullptr };
    Buffer* m_sphereIndexBuffer { nullptr };
    uint32_t m_indexCount { 0u };
};
