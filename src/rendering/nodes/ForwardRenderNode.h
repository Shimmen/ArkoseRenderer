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
    struct ForwardVertex {
        vec3 position;
        vec2 texCoord;
        vec3 normal;
        vec4 tangent;
    };

    VertexLayout vertexLayout {
        sizeof(ForwardVertex),
        { { 0, VertexAttributeType::Float3, offsetof(ForwardVertex, position) },
          { 1, VertexAttributeType::Float2, offsetof(ForwardVertex, texCoord) },
          { 2, VertexAttributeType ::Float3, offsetof(ForwardVertex, normal) },
          { 3, VertexAttributeType ::Float4, offsetof(ForwardVertex, tangent) } }
    };

    SemanticVertexLayout semanticVertexLayout { VertexComponent::Position3F,
                                                VertexComponent::TexCoord2F,
                                                VertexComponent::Normal3F,
                                                VertexComponent::Tangent4F };

    BindingSet* m_indirectLightBindingSet { nullptr };

    Scene& m_scene;
};
