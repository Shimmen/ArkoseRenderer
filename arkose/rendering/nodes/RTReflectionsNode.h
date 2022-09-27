#pragma once

#include "rendering/RenderPipelineNode.h"

class RTReflectionsNode final : public RenderPipelineNode {
public:

    std::string name() const override { return "RT reflections"; }
    void drawGui() override;

    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    float m_injectedAmbient { 500.0f };

    float m_mirrorRoughnessThreshold { 0.001f };
    float m_fullyDiffuseRoughnessThreshold { 0.6f };

};
