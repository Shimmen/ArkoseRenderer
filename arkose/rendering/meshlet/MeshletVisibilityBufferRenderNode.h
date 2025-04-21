#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/meshlet/MeshletIndirectHelper.h"

class MeshletVisibilityBufferRenderNode final : public RenderPipelineNode {
public:
    std::string name() const override { return "Meshlet visibility buffer"; }
    void drawGui() override;

    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    MeshletIndirectHelper m_meshletIndirectHelper {};
    bool m_frustumCullInstances { false }; // Keep default off (for now!)
    bool m_frustumCullMeshlets { true };

    struct PassSettings {
        DrawKey drawKeyMask {};
        u32 maxMeshlets { 10'000 };
        std::string debugName {};
        bool firstPass { false };
    };

    struct RenderStateWithIndirectData {
        RenderState* renderState { nullptr };
        MeshletIndirectBuffer* indirectBuffer { nullptr };
    };

    RenderTarget& makeRenderTarget(Registry&, LoadOp loadOp) const;
    RenderStateWithIndirectData& makeRenderState(Registry&, GpuScene const&, PassSettings) const;
    std::vector<RenderStateWithIndirectData*>& createRenderStates(Registry&, GpuScene const&) const;
};
