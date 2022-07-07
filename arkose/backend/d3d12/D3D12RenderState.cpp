#include "D3D12RenderState.h"

#include "utility/Profiling.h"

D3D12RenderState::D3D12RenderState(Backend& backend, const RenderTarget& renderTarget, VertexLayout vertexLayout,
                                   Shader shader, const StateBindings& stateBindings,
                                   BlendState blendState, RasterState rasterState, DepthState depthState, StencilState stencilState)
    : RenderState(backend, renderTarget, vertexLayout, shader, stateBindings, blendState, rasterState, depthState, stencilState)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();
    // TODO
}

D3D12RenderState::~D3D12RenderState()
{
    if (!hasBackend())
        return;
    // TODO
}

void D3D12RenderState::setName(const std::string& name)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    Resource::setName(name);

    // TODO
}
