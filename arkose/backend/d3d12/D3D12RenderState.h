#pragma once

#include "backend/base/RenderState.h"

struct D3D12RenderState final : public RenderState {
public:
    D3D12RenderState(Backend&, const RenderTarget&, VertexLayout, Shader, const StateBindings& stateBindings,
                     Viewport, BlendState, RasterState, DepthState, StencilState);
    virtual ~D3D12RenderState() override;

    virtual void setName(const std::string& name) override;
};
