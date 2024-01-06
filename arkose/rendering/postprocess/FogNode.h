#pragma once

#include "core/Types.h"
#include "rendering/RenderPipelineNode.h"
#include <ark/vector.h>

class FogNode final : public RenderPipelineNode {
public:
    FogNode();

    std::string name() const override { return "Fog"; }
    void drawGui() override;

    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    bool m_enabled { true };
    float m_fogDensity { 0.0007f };
    vec3 m_fogColor { 0.5f, 0.6f, 0.7f };
};
