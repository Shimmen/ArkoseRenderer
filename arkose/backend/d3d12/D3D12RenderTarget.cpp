#include "D3D12RenderTarget.h"

#include "utility/Profiling.h"

D3D12RenderTarget::D3D12RenderTarget(Backend& backend, std::vector<Attachment> attachments)
    : RenderTarget(backend, std::move(attachments))
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    // TODO
}

D3D12RenderTarget::~D3D12RenderTarget()
{
    if (!hasBackend())
        return;
    // TODO
}

void D3D12RenderTarget::setName(const std::string& name)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    Resource::setName(name);

    // TODO
}
