#pragma once

#include "rendering/DrawKey.h"
#include "rendering/RenderPipelineNode.h"
#include "rendering/VertexManager.h"
#include "rendering/forward/ForwardModes.h"

class CommandList;
struct StaticMeshSegment;

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

    struct BufferStates {
        std::vector<std::pair<Buffer const*, VertexLayout>> vertexBuffers {};
        std::pair<Buffer const*, IndexType> indexBuffer {};
        bool operator==(BufferStates const& other) const = default;
        bool hasAnyBuffers() const { return !vertexBuffers.empty() && indexBuffer.first != nullptr; }
    };

    struct HairDrawData {
        u32 drawableIdx {};
        u32 firstStrand {};
        u32 strandCount {};
    };

    struct MeshSegmentInstance {
        MeshSegmentInstance(DrawCallDescription, DrawKey, BufferStates, Transform const&, std::optional<HairDrawData> = std::nullopt);
        DrawCallDescription drawCall {};
        DrawKey drawKey {};
        BufferStates bufferStates {};
        Transform const* transform {};
        std::optional<HairDrawData> hairDrawData {};
    };

    RenderTarget& makeRenderTarget(Registry&, Mode) const;
    RenderState& makeForwardRenderState(Registry&, GpuScene const&, RenderTarget const&, DrawKey const&) const;

    std::vector<MeshSegmentInstance> generateSortedDrawList(GpuScene const&, Mode, ForwardMeshFilter) const;
};
