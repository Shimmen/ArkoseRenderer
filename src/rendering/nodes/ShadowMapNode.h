#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/camera/FpsCamera.h"
#include "rendering/scene/Model.h"
#include "rendering/scene/Scene.h"

class ShadowMapNode final : public RenderGraphNode {
public:
    explicit ShadowMapNode(Scene&);
    ~ShadowMapNode() override = default;

    static std::string name();

    std::optional<std::string> displayName() const override { return "Shadow Mapping"; }

    ExecuteCallback constructFrame(Registry&) const override;

private:
    VertexLayout m_vertexLayout { VertexComponent::Position3F,
                                  VertexComponent::Normal3F };
    Scene& m_scene;
};
