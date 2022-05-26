#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/scene/Model.h"
#include "rendering/scene/GpuScene.h"

class ShadowMapNode final : public RenderPipelineNode {
public:

    std::string name() const override { return "Shadow Mapping"; }
    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    VertexLayout m_vertexLayout { VertexComponent::Position3F,
                                  VertexComponent::Normal3F };
};
