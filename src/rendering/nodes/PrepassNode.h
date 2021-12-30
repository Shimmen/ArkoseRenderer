#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/scene/Scene.h"

class PrepassNode final : public RenderPipelineNode {
public:
    explicit PrepassNode(Scene&);

    std::string name() const override { return "Prepass"; }

    ExecuteCallback constructFrame(Registry&) const override;

private:
    VertexLayout m_prepassVertexLayout { VertexComponent::Position3F };
    Scene& m_scene;
};
