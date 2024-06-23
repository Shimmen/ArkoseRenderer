#pragma once

#include "rendering/backend/base/ComputeState.h"

#include <d3d12.h>
#include <wrl/client.h>

struct D3D12ComputeState final : public ComputeState {
public:
    D3D12ComputeState(Backend&, Shader, StateBindings const&);
    virtual ~D3D12ComputeState() override;

    virtual void setName(const std::string& name) override;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature {};
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso {};
};
