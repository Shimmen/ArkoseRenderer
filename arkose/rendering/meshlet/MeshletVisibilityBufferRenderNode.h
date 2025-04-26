#pragma once

#include "core/math/Frustum.h"
#include "rendering/RenderPipelineNode.h"
#include "rendering/meshlet/MeshletIndirectHelper.h"

class MeshletVisibilityBufferRenderNode : public RenderPipelineNode {
public:
    std::string name() const override { return "Meshlet visibility buffer"; }
    void drawGui() override;

    ExecuteCallback construct(GpuScene&, Registry&) override;

protected:
    MeshletIndirectHelper m_meshletIndirectHelper {};
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

    virtual bool usingDepthBias() const { return false; }
    virtual vec2 depthBiasParameters(GpuScene&) const { return vec2(0.0f, 0.0f); }

    virtual mat4 calculateViewProjectionMatrix(GpuScene&) const;
    virtual geometry::Frustum calculateCullingFrustum(GpuScene&) const;

    virtual RenderTarget& makeRenderTarget(Registry&, LoadOp loadOp) const;
    virtual Shader makeShader(BlendMode, std::vector<ShaderDefine> const& shaderDefines) const;

    RenderStateWithIndirectData& makeRenderState(Registry&, GpuScene const&, PassSettings) const;
    std::vector<RenderStateWithIndirectData*>& createRenderStates(Registry&, GpuScene const&) const;
};
