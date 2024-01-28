#pragma once

#include "rendering/backend/base/RenderState.h"

#include "rendering/backend/d3d12/D3D12Common.h"

struct D3D12RenderState final : public RenderState {
public:
    D3D12RenderState(Backend&, RenderTarget const&, std::vector<VertexLayout>, Shader, StateBindings const&,
                     RasterState, DepthState, StencilState);
    virtual ~D3D12RenderState() override;

    virtual void setName(const std::string& name) override;

    template<typename Callback>
    void forEachRootParameterToBind(Callback&&) const;

    std::vector<D3D12_ROOT_PARAMETER> boundRootParameters {};
    std::vector<ShaderBinding const*> boundShaderBindings {};

    ComPtr<ID3D12RootSignature> rootSignature {};

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc {};
    ComPtr<ID3D12PipelineState> pso {};
};

template<typename Callback>
void D3D12RenderState::forEachRootParameterToBind(Callback&& callback) const
{
    size_t rootParameterCount = boundRootParameters.size();
    ARKOSE_ASSERT(rootParameterCount == boundShaderBindings.size());

    for (size_t rootParameterIdx = 0; rootParameterIdx < rootParameterCount; ++rootParameterIdx) {

        D3D12_ROOT_PARAMETER const& rootParam = boundRootParameters[rootParameterIdx];
        ShaderBinding const* binding = boundShaderBindings[rootParameterIdx];

        callback(rootParam, *binding);
    }
}
