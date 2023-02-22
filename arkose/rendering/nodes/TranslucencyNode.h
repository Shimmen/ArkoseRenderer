#pragma once

#include "rendering/RenderPipelineNode.h"

class Transform;
struct StaticMeshSegment;

class TranslucencyNode final : public RenderPipelineNode {
public:
    std::string name() const override { return "Translucency"; }
    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    VertexLayout m_vertexLayout { VertexComponent::Position3F,
                                  VertexComponent::TexCoord2F,
                                  VertexComponent::Normal3F,
                                  VertexComponent::Tangent4F };

    struct TranslucentMeshSegmentInstance {
        TranslucentMeshSegmentInstance(StaticMeshSegment const&, Transform const&, u32 drawableIdx);
        StaticMeshSegment const* meshSegment {};
        Transform const* transform {};
        u32 drawableIdx { 0 };
    };

    std::vector<TranslucentMeshSegmentInstance> generateSortedDrawList(GpuScene const&) const;
    RenderState& makeRenderState(Registry&, GpuScene const&, RenderTarget&) const;
};
