#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/GpuScene.h"

class GIComposeNode final : public RenderPipelineNode {
public:
    std::string name() const override { return "GI Compose"; }
    void drawGui() override;

    ExecuteCallback construct(GpuScene&, Registry&) override;

    enum class ComposeMode {
        FullCompose,
        DirectOnly,
        DiffuseIndirectOnly,
        DiffuseIndirectOnlyNoBaseColor,
        GlossyIndirectOnly,
    };

    ComposeMode composeMode() const { return m_composeMode; }
    void setComposeMode(ComposeMode mode) { m_composeMode = mode; }

private:
    ComposeMode m_composeMode { ComposeMode::FullCompose };
    bool m_includeAmbientOcclusion { true };
};
