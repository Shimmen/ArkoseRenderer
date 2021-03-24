#pragma once

#include "../RenderGraphNode.h"
#include "rendering/camera/FpsCamera.h"
#include "rendering/scene/Model.h"
#include "rendering/scene/Scene.h"

class ForwardRenderNode final : public RenderGraphNode {
public:
    explicit ForwardRenderNode(Scene&);

    std::optional<std::string> displayName() const override { return "Forward"; }
    static std::string name();

    void constructNode(Registry&) override;
    ExecuteCallback constructFrame(Registry&) const override;

private:
    VertexLayout m_prepassVertexLayout { VertexComponent::Position3F };
    VertexLayout m_vertexLayout { VertexComponent::Position3F,
                                  VertexComponent::TexCoord2F,
                                  VertexComponent::Normal3F,
                                  VertexComponent::Tangent4F };

    BindingSet* m_indirectLightBindingSet { nullptr };

    Scene& m_scene;
};
