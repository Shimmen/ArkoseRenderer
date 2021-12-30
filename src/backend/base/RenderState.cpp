#include "RenderState.h"

RenderStateBuilder::RenderStateBuilder(const RenderTarget& renderTarget, const Shader& shader, VertexLayout vertexLayout)
    : renderTarget(renderTarget)
    , shader(shader)
    , vertexLayout(vertexLayout)
{
}

Viewport RenderStateBuilder::viewport() const
{
    if (m_viewport.has_value()) {
        return m_viewport.value();
    }

    Viewport view {
        .x = 0.0f,
        .y = 0.0f,
        .extent = renderTarget.extent()
    };
    return view;
}

BlendState RenderStateBuilder::blendState() const
{
    if (m_blendState.has_value()) {
        return m_blendState.value();
    }

    BlendState state {
        .enabled = false
    };
    return state;
}

RasterState RenderStateBuilder::rasterState() const
{
    if (m_rasterState.has_value()) {
        return m_rasterState.value();
    }

    RasterState state {
        .backfaceCullingEnabled = cullBackfaces,
        .frontFace = frontFace,
        .polygonMode = polygonMode
    };
    return state;
}

DepthState RenderStateBuilder::depthState() const
{
    DepthState state {
        .writeDepth = writeDepth,
        .testDepth = testDepth,
        .compareOp = depthCompare,
    };
    return state;
}

StencilState RenderStateBuilder::stencilState() const
{
    StencilState state {
        .mode = stencilMode
    };
    return state;
}
