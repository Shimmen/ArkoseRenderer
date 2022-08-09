#pragma once

#include "core/math/Frustum.h"
#include "rendering/RenderPipelineNode.h"
#include <vector>
#include <ark/rect.h>

class Light;

class LocalLightShadowNode final : public RenderPipelineNode {
public:
    std::string name() const override { return "Local light shadows"; }
    void drawGui() override;

    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    VertexLayout m_vertexLayout { VertexComponent::Position3F,
                                  VertexComponent::Normal3F };

    int m_maxNumShadowMaps { 16 };

    // Any shadow map smaller than this is not worth rendering
    ivec2 m_minimumViableShadowMapSize { 16, 16 };

    struct ShadowMapAtlasAllocation {
        const Light* light { nullptr };
        Rect2D rect {};
    };

    std::vector<ShadowMapAtlasAllocation> allocateShadowMapsInAtlas(const GpuScene&, const Texture& atlas) const;
    std::vector<vec4> collectAtlasViewportDataForAllocations(const GpuScene&, Extent2D atlasExtent, const std::vector<ShadowMapAtlasAllocation>&) const;

    void drawSpotLightShadowMap(CommandList&, GpuScene&, const ShadowMapAtlasAllocation&) const;
    void drawShadowCasters(CommandList&, GpuScene&, geometry::Frustum& lightFrustum) const;
};
