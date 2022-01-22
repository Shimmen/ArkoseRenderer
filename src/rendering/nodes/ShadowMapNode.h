#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/camera/FpsCamera.h"
#include "rendering/scene/Model.h"
#include "rendering/scene/Scene.h"

class ShadowMapNode final : public RenderPipelineNode {
public:
    explicit ShadowMapNode(Scene&);
    ~ShadowMapNode() override = default;

    std::string name() const override { return "Shadow Mapping"; }

    ExecuteCallback construct(Registry&) override;

private:
    VertexLayout m_vertexLayout { VertexComponent::Position3F,
                                  VertexComponent::Normal3F };
    Scene& m_scene;
};
