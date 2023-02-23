#pragma once

#include "rendering/backend/base/RenderState.h"

struct D3D12RenderState final : public RenderState {
public:
    D3D12RenderState(Backend&, const RenderTarget&, VertexLayout, Shader, const StateBindings& stateBindings,
                     RasterState, DepthState, StencilState);
    virtual ~D3D12RenderState() override;

    virtual void setName(const std::string& name) override;
};
