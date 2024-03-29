#pragma once

#include "rendering/backend/base/ComputeState.h"

struct D3D12ComputeState final : public ComputeState {
public:
    D3D12ComputeState(Backend&, Shader, StateBindings const&);
    virtual ~D3D12ComputeState() override;

    virtual void setName(const std::string& name) override;
};
