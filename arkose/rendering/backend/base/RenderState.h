#pragma once

#include "rendering/backend/Resource.h"
#include "rendering/backend/base/RenderTarget.h"
#include "rendering/backend/shader/Shader.h"
#include "rendering/backend/util/StateBindings.h"

// TODO: Clean up: shouln't refer to frontend from backend!
#include "scene/Vertex.h"

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

enum class PrimitiveType {
    Triangles,
    LineSegments,
    Points,
};

enum class PolygonMode {
    Filled,
    Lines,
    Points
};

struct RasterState {
    bool backfaceCullingEnabled { true };
    bool depthBiasEnabled { false };
    TriangleWindingOrder frontFace { TriangleWindingOrder::CounterClockwise };
    PrimitiveType primitiveType { PrimitiveType::Triangles };
    PolygonMode polygonMode { PolygonMode::Filled };
    float lineWidth { 1.0f };
};

class RenderState : public Resource {
public:
    RenderState() = default;
    RenderState(Backend& backend,
                const RenderTarget& renderTarget, VertexLayout vertexLayout,
                Shader shader, const StateBindings& stateBindings,
                RasterState rasterState, DepthState depthState, StencilState stencilState)
        : Resource(backend)
        , m_renderTarget(&renderTarget)
        , m_vertexLayout(vertexLayout)
        , m_shader(shader)
        , m_stateBindings(stateBindings)
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

    const RasterState& rasterState() const { return m_rasterState; }
    const DepthState& depthState() const { return m_depthState; }
    const StencilState& stencilState() const { return m_stencilState; }

private:
    const RenderTarget* m_renderTarget;
    VertexLayout m_vertexLayout;

    Shader m_shader;
    StateBindings m_stateBindings;

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
    PrimitiveType primitiveType { PrimitiveType::Triangles };
    PolygonMode polygonMode { PolygonMode::Filled };
    float lineWidth { 1.0f };

    bool enableDepthBias { false };

    bool cullBackfaces { true };
    TriangleWindingOrder frontFace { TriangleWindingOrder::CounterClockwise };

    [[nodiscard]] RasterState rasterState() const;
    [[nodiscard]] DepthState depthState() const;
    [[nodiscard]] StencilState stencilState() const;

    const StateBindings& stateBindings() const { return m_stateBindings; }
    StateBindings& stateBindings() { return m_stateBindings; }

private:
    std::optional<RasterState> m_rasterState {};
    StateBindings m_stateBindings {};
};
