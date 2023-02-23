#include "RenderState.h"

RenderStateBuilder::RenderStateBuilder(const RenderTarget& renderTarget, const Shader& shader, VertexLayout vertexLayout)
    : renderTarget(renderTarget)
    , shader(shader)
    , vertexLayout(vertexLayout)
{
}

RasterState RenderStateBuilder::rasterState() const
{
    if (m_rasterState.has_value()) {
        return m_rasterState.value();
    }

    RasterState state {
        .backfaceCullingEnabled = cullBackfaces,
        .depthBiasEnabled = enableDepthBias,
        .frontFace = frontFace,
        .primitiveType = primitiveType,
        .polygonMode = polygonMode,
        .lineWidth = lineWidth
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
