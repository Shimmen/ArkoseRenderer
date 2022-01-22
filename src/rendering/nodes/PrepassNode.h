#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/scene/Scene.h"

class PrepassNode final : public RenderPipelineNode {
public:

    std::string name() const override { return "Prepass"; }
    ExecuteCallback construct(Scene&, Registry&) override;

private:
    VertexLayout m_prepassVertexLayout { VertexComponent::Position3F };
};
