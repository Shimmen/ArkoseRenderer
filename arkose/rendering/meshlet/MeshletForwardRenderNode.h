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

    enum class PassBlendMode {
        Opaque,
        Masked,
    };

    struct PassSettings {
        std::string debugName {};
        u32 maxMeshlets { 10'000 };
        PassBlendMode blendMode;
        bool doubleSided;
    };

    struct RenderStateWithIndirectData {
        RenderState* renderState { nullptr };
        Buffer* indirectDataBuffer { nullptr };
    };

    RenderTarget& makeRenderTarget(Registry&, LoadOp) const;
    RenderStateWithIndirectData& makeRenderState(Registry&, const GpuScene&, PassSettings) const;
};
