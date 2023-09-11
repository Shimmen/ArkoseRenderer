#pragma once

#include "rendering/DrawKey.h"
#include "rendering/RenderPipelineNode.h"
#include "rendering/VertexManager.h"
#include "rendering/forward/ForwardModes.h"

class CommandList;
class StaticMeshSegment;

class ForwardRenderNode final : public RenderPipelineNode {
public:
    enum class Mode {
        Opaque,
        Translucent,
    };

    ForwardRenderNode(Mode = Mode::Opaque,
                      ForwardMeshFilter = ForwardMeshFilter::AllMeshes,
                      ForwardClearMode = ForwardClearMode ::ClearBeforeFirstDraw);

    std::string name() const override;
    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    Mode m_mode;
    ForwardMeshFilter m_meshFilter;
    ForwardClearMode m_clearMode;
    bool m_hasPreviousPrepass { false };

    struct MeshSegmentInstance {
        MeshSegmentInstance(VertexAllocation, DrawKey, Transform const&, u32 drawableIdx);
        VertexAllocation vertexAllocation {};
        DrawKey drawKey {};
        u32 drawableIdx { 0 };
        Transform const* transform {};
    };

    RenderTarget& makeRenderTarget(Registry&, Mode) const;
    RenderState& makeForwardRenderState(Registry&, GpuScene const&, RenderTarget const&, DrawKey const&) const;

    std::vector<MeshSegmentInstance> generateSortedDrawList(GpuScene const&, Mode, ForwardMeshFilter) const;
};
