#pragma once

#include "../RenderGraphNode.h"
#include "rendering/scene/Scene.h"

class PrepassNode final : public RenderGraphNode {
public:
    explicit PrepassNode(Scene&);

    std::optional<std::string> displayName() const override { return "Prepass"; }
    static std::string name();

    ExecuteCallback constructFrame(Registry&) const override;

private:
    VertexLayout m_prepassVertexLayout { VertexComponent::Position3F };
    Scene& m_scene;
};
