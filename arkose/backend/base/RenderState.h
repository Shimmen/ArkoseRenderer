#pragma once

#include "backend/Resource.h"
#include "backend/base/RenderTarget.h"
#include "backend/shader/Shader.h"
#include "backend/util/StateBindings.h"

// TODO: Clean up: shouln't refer to frontend from backend!
#include "rendering/scene/Vertex.h"

struct BlendState {
    bool enabled { false };
};

enum class DepthCompareOp {
    Less,
    LessThanEqual,
    Greater,
    GreaterThanEqual,
    Equal,
};

struct DepthState {
    bool writeDepth { true };
    bool testDepth { true };
    DepthCompareOp compareOp { DepthCompareOp::Less };
};

// Instead of exposing the whole stencil interface we will just have some presets/modes (at least for now!)
enum class StencilMode {
    Disabled,

    // Writing modes
    AlwaysWrite,

    // Non-writing modes
    PassIfZero,
    PassIfNotZero,
};

struct StencilState {
    StencilMode mode { StencilMode::Disabled };
};

enum class TriangleWindingOrder {
    Clockwise,
    CounterClockwise
};

enum class PolygonMode {
    Filled,
    Lines,
    Points
};

struct RasterState {
    bool backfaceCullingEnabled { true };
    TriangleWindingOrder frontFace { TriangleWindingOrder::CounterClockwise };
    PolygonMode polygonMode { PolygonMode::Filled };
};

struct Viewport {
    float x { 0.0f };
    float y { 0.0f };
    Extent2D extent;
};

class RenderState : public Resource {
public:
    RenderState() = default;
    RenderState(Backend& backend,
                const RenderTarget& renderTarget, VertexLayout vertexLayout,
                Shader shader, const StateBindings& stateBindings,
                Viewport viewport, BlendState blendState, RasterState rasterState, DepthState depthState, StencilState stencilState)
        : Resource(backend)
        , m_renderTarget(&renderTarget)
        , m_vertexLayout(vertexLayout)
        , m_shader(shader)
        , m_stateBindings(stateBindings)
        , m_viewport(viewport)
        , m_blendState(blendState)
        , m_rasterState(rasterState)
        , m_depthState(depthState)
        , m_stencilState(stencilState)
    {
        ARKOSE_ASSERT(shader.type() == ShaderType::Raster);
    }

    const RenderTarget& renderTarget() const { return *m_renderTarget; }
    const VertexLayout& vertexLayout() const { return m_vertexLayout; }

    const Shader& shader() const { return m_shader; }
    const StateBindings& stateBindings() const { return m_stateBindings; }

    const Viewport& fixedViewport() const { return m_viewport; }
    const BlendState& blendState() const { return m_blendState; }
    const RasterState& rasterState() const { return m_rasterState; }
    const DepthState& depthState() const { return m_depthState; }
    const StencilState& stencilState() const { return m_stencilState; }

private:
    const RenderTarget* m_renderTarget;
    VertexLayout m_vertexLayout;

    Shader m_shader;
    StateBindings m_stateBindings;

    Viewport m_viewport;
    BlendState m_blendState;
    RasterState m_rasterState;
    DepthState m_depthState;
    StencilState m_stencilState;
};

class RenderStateBuilder {
public:
    RenderStateBuilder(const RenderTarget&, const Shader&, VertexLayout);

    const RenderTarget& renderTarget;
    const Shader& shader;
    VertexLayout vertexLayout;

    bool writeDepth { true };
    bool testDepth { true };
    DepthCompareOp depthCompare { DepthCompareOp::Less };
    StencilMode stencilMode { StencilMode::Disabled };
    PolygonMode polygonMode { PolygonMode::Filled };

    bool cullBackfaces { true };
    TriangleWindingOrder frontFace { TriangleWindingOrder::CounterClockwise };

    [[nodiscard]] Viewport viewport() const;
    [[nodiscard]] BlendState blendState() const;
    [[nodiscard]] RasterState rasterState() const;
    [[nodiscard]] DepthState depthState() const;
    [[nodiscard]] StencilState stencilState() const;

    const StateBindings& stateBindings() const { return m_stateBindings; }
    StateBindings& stateBindings() { return m_stateBindings; }

private:
    std::optional<Viewport> m_viewport {};
    std::optional<BlendState> m_blendState {};
    std::optional<RasterState> m_rasterState {};
    StateBindings m_stateBindings {};
};
