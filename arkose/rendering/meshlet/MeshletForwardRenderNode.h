#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/meshlet/MeshletIndirectHelper.h"

class MeshletForwardRenderNode final : public RenderPipelineNode {
public:
    std::string name() const override { return "Meshlet forward render"; }
    void drawGui() override;

    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    MeshletIndirectHelper m_meshletIndirectHelper {};
    bool m_frustumCullInstances { false }; // Keep default off (for now!)
    bool m_frustumCullMeshlets { true };

    struct PassSettings {
        std::string debugName {};
        u32 maxMeshlets { 10'000 };
        DrawKey drawKeyMask {};
    };

    struct RenderStateWithIndirectData {
        RenderState* renderState { nullptr };
        MeshletIndirectBuffer* indirectBuffer { nullptr };
    };

    RenderTarget& makeRenderTarget(Registry&, LoadOp) const;
    RenderStateWithIndirectData& makeRenderState(Registry&, const GpuScene&, PassSettings) const;
};
