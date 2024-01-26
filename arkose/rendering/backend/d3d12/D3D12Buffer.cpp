#include "D3D12Buffer.h"

#include "rendering/backend/d3d12/D3D12Backend.h"
#include "core/Logging.h"
#include "utility/Profiling.h"

D3D12Buffer::D3D12Buffer(Backend& backend, size_t size, Usage usage, MemoryHint memoryHint)
    : Buffer(backend, size, usage, memoryHint)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    auto& d3d12Backend = static_cast<D3D12Backend&>(backend);

    CD3DX12_HEAP_PROPERTIES heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_RESOURCE_FLAGS resourceFlags = D3D12_RESOURCE_FLAG_NONE;
    
    D3D12_RESOURCE_STATES initialResourceState = D3D12_RESOURCE_STATE_COMMON;
    
    switch (usage) {
    case Buffer::Usage::ConstantBuffer:
        initialResourceState = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        break;
    case Buffer::Usage::StorageBuffer:
        initialResourceState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        resourceFlags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        break;
    case Buffer::Usage::IndirectBuffer:
        initialResourceState = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        resourceFlags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        break;
    case Buffer::Usage::Vertex:
        initialResourceState = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        resourceFlags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        break;
    case Buffer::Usage::Index:
        initialResourceState = D3D12_RESOURCE_STATE_INDEX_BUFFER;
        resourceFlags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        break;
    case Buffer::Usage::RTInstanceBuffer:
        initialResourceState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        resourceFlags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        break;
    case Buffer::Usage::Transfer:
        // "When you create a resource together with a D3D12_HEAP_TYPE_UPLOAD heap, you must set InitialResourceState to D3D12_RESOURCE_STATE_GENERIC_READ."
        // From: https://docs.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createcommittedresource
        heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        initialResourceState = D3D12_RESOURCE_STATE_GENERIC_READ;
        break;
    default:
        ASSERT_NOT_REACHED();
    }

    CD3DX12_RESOURCE_DESC bufferDescription = CD3DX12_RESOURCE_DESC::Buffer(size, resourceFlags);

    // TODO: Don't use commited resource! Sub-allocate instead
    auto hr = d3d12Backend.device().CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE,
                                                            &bufferDescription, initialResourceState, nullptr,
                                                            IID_PPV_ARGS(&bufferResource));
    if (FAILED(hr)) {
        ARKOSE_LOG(Fatal, "D3D12Buffer: could not create committed resource for buffer, exiting.");
    }

    resourceState = initialResourceState;

    // TODO: Actually track the allocated size, not just what we asked for
    m_sizeInMemory = size;
}

D3D12Buffer::~D3D12Buffer()
{
}

void D3D12Buffer::setName(const std::string& name)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    Resource::setName(name);
    bufferResource->SetName(convertToWideString(name).c_str());
}

void D3D12Buffer::updateData(const std::byte* data, size_t updateSize, size_t offset)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    if (updateSize == 0) {
        return;
    }
    if (offset + updateSize > size()) {
        ARKOSE_LOG(Fatal, "Attempt at updating buffer outside of bounds!");
    }

    auto& d3d12Backend = static_cast<D3D12Backend&>(backend());

    switch (memoryHint()) {
    case Buffer::MemoryHint::GpuOptimal:
        if (!d3d12Backend.setBufferDataUsingStagingBuffer(*this, (uint8_t*)data, updateSize, offset)) {
            ARKOSE_LOG(Error, "Could not update the data of GPU-optimal buffer");
        }
        break;
    case Buffer::MemoryHint::TransferOptimal:
        if (!d3d12Backend.setBufferDataUsingMapping(*bufferResource.Get(), (uint8_t*)data, updateSize, offset)) {
            ARKOSE_LOG(Error, "Could not update the data of transfer-optimal buffer");
        }
        break;
    case Buffer::MemoryHint::GpuOnly:
        ARKOSE_LOG(Error, "Can't update buffer with GpuOnly memory hint, ignoring");
        break;
    case Buffer::MemoryHint::Readback:
        ARKOSE_LOG(Error, "Can't update buffer with Readback memory hint, ignoring");
        break;
    }
}

void D3D12Buffer::reallocateWithSize(size_t newSize, ReallocateStrategy strategy)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();
    
    ASSERT_NOT_REACHED();
}
