#pragma once

#include "rendering/meshlet/MeshletDepthOnlyRenderNode.h"

class DirectionalShadowDrawNode final : public MeshletDepthOnlyRenderNode {
public:
    std::string name() const override { return "Directional light shadow"; }
    void drawGui() override;

    ExecuteCallback construct(GpuScene&, Registry&) override;

protected:
    bool usingDepthBias() const override { return true; }
    vec2 depthBiasParameters(GpuScene&) const override;

    mat4 calculateViewProjectionMatrix(GpuScene&) const override;
    geometry::Frustum calculateCullingFrustum(GpuScene&) const override;

    RenderTarget& makeRenderTarget(Registry&, LoadOp loadOp) const override;

private:
    Texture* m_shadowMap { nullptr };
};
