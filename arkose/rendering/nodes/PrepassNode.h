#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/GpuScene.h"
#include "rendering/forward/ForwardModes.h"

class PrepassNode final : public RenderPipelineNode {
public:

    PrepassNode(ForwardMeshFilter = ForwardMeshFilter::AllMeshes,
                ForwardClearMode = ForwardClearMode ::ClearBeforeFirstDraw);

    std::string name() const override { return "Prepass"; }
    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    ForwardMeshFilter m_meshFilter;
    ForwardClearMode m_clearMode;

    struct MeshSegmentInstance {
        MeshSegmentInstance(VertexAllocation, DrawKey, u32 drawableIdx);
        VertexAllocation vertexAllocation {};
        DrawKey drawKey {};
        u32 drawableIdx { 0 };
    };

    RenderState& makeRenderState(Registry&, GpuScene const&, RenderTarget const&, DrawKey const&) const;

    std::vector<MeshSegmentInstance> generateDrawList(GpuScene const&, ForwardMeshFilter) const;
};
