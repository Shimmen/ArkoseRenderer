#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/GpuScene.h"

class LightingComposeNode final : public RenderPipelineNode {
public:
    std::string name() const override { return "Lighting compose"; }
    void drawGui() override;

    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    GpuScene* m_scene { nullptr };

    bool m_includeSpecularDirectLight { true };
    bool m_includeDiffuseDirectLight { true };

    bool m_includeGlossyGI { true };

    bool m_includeDiffuseGI { true };
    bool m_withBakedOcclusion { true };
    bool m_useBentNormalDirection { true };
    bool m_withBentNormalOcclusion { true };
    bool m_withScreenSpaceOcclusion { true };

    BindingSet* m_ddgiBindingSet { nullptr };
    bool m_hasScreenSpaceOcclusionTexture { false };
};
