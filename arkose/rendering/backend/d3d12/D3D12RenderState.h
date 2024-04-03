#pragma once

#include "rendering/backend/base/RenderState.h"

#include "rendering/backend/d3d12/D3D12Common.h"

struct D3D12RenderState final : public RenderState {
public:
    D3D12RenderState(Backend&, RenderTarget const&, std::vector<VertexLayout>, Shader, StateBindings const&,
                     RasterState, DepthState, StencilState);
    virtual ~D3D12RenderState() override;

    virtual void setName(const std::string& name) override;

    ComPtr<ID3D12RootSignature> rootSignature {};

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc {};
    ComPtr<ID3D12PipelineState> pso {};
};
