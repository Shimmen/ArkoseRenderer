#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/GpuScene.h"

class PrepassNode final : public RenderPipelineNode {
public:

    std::string name() const override { return "Prepass"; }
    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    VertexLayout m_prepassVertexLayout { VertexComponent::Position3F };
};
