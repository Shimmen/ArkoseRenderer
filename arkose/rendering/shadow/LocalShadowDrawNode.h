#pragma once

#include "rendering/meshlet/MeshletDepthOnlyRenderNode.h"
#include <vector>
#include <ark/rect.h>

class Light;

class LocalShadowDrawNode final : public MeshletDepthOnlyRenderNode {
public:
    std::string name() const override { return "Local light shadows"; }
    void drawGui() override;

    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    Texture* m_shadowMapAtlas { nullptr };

    int m_maxNumShadowMaps { 16 };

    // Any shadow map smaller than this is not worth rendering
    ivec2 m_minimumViableShadowMapSize { 16, 16 };

    struct ShadowMapAtlasAllocation {
        Light const* light { nullptr };
        Rect2D rect {};
    };

    std::vector<ShadowMapAtlasAllocation> allocateShadowMapsInAtlas(const GpuScene&, const Texture& atlas) const;
    std::vector<vec4> collectAtlasViewportDataForAllocations(const GpuScene&, Extent2D atlasExtent, const std::vector<ShadowMapAtlasAllocation>&) const;

    RenderTarget& makeRenderTarget(Registry&, LoadOp loadOp) const override;

    bool usingDepthBias() const override { return true; }
    vec2 depthBiasParameters(GpuScene&) const override
    {
        // Done manually for this node
        ASSERT_NOT_REACHED();
    }
};
