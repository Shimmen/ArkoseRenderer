#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/camera/FpsCamera.h"
#include "rendering/scene/Model.h"
#include "rendering/scene/Scene.h"

class ForwardRenderNode final : public RenderPipelineNode {
public:

    std::string name() const override { return "Forward"; }
    ExecuteCallback construct(Scene&, Registry&) override;

private:
    VertexLayout m_vertexLayout { VertexComponent::Position3F,
                                  VertexComponent::TexCoord2F,
                                  VertexComponent::Normal3F,
                                  VertexComponent::Tangent4F };
};
