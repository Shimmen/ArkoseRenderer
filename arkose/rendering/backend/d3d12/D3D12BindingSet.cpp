#include "D3D12BindingSet.h"

#include "utility/Profiling.h"

D3D12BindingSet::D3D12BindingSet(Backend& backend, std::vector<ShaderBinding> bindings)
    : BindingSet(backend, std::move(bindings))
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    // TODO
}

D3D12BindingSet::~D3D12BindingSet()
{
    if (!hasBackend())
        return;
    // TODO
}

void D3D12BindingSet::setName(const std::string& name)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    Resource::setName(name);

    // TODO
}

void D3D12BindingSet::updateTextures(uint32_t bindingIndex, const std::vector<TextureBindingUpdate>& textureUpdates)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    // TODO
}
