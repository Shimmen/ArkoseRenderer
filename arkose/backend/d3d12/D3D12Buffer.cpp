#include "D3D12Buffer.h"

#include "utility/Profiling.h"

D3D12Buffer::D3D12Buffer(Backend& backend, size_t size, Usage usage, MemoryHint memoryHint)
    : Buffer(backend, size, usage, memoryHint)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();
    // TODO
}

D3D12Buffer::~D3D12Buffer()
{
}

void D3D12Buffer::setName(const std::string& name)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    Resource::setName(name);

    // TODO
}

void D3D12Buffer::updateData(const std::byte* data, size_t updateSize, size_t offset)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    // TODO
}

void D3D12Buffer::reallocateWithSize(size_t newSize, ReallocateStrategy strategy)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();
    
    // TODO
}
