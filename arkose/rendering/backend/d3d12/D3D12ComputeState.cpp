#include "D3D12ComputeState.h"

#include "utility/Profiling.h"

D3D12ComputeState::D3D12ComputeState(Backend& backend, Shader shader, StateBindings const& stateBindings)
    : ComputeState(backend, shader, stateBindings)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();
}

D3D12ComputeState::~D3D12ComputeState()
{
    if (!hasBackend())
        return;
    // TODO
}

void D3D12ComputeState::setName(const std::string& name)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    Resource::setName(name);

    // TODO
}
