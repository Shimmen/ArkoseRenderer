#include "D3D12Buffer.h"

#include "rendering/backend/d3d12/D3D12Backend.h"
#include "core/Logging.h"
#include "utility/Profiling.h"

D3D12Buffer::D3D12Buffer(Backend& backend, size_t size, Usage usage)
    : Buffer(backend, size, usage)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    auto& d3d12Backend = static_cast<D3D12Backend&>(backend);
    
    D3D12_RESOURCE_STATES initialResourceState = D3D12_RESOURCE_STATE_COMMON;
    
    D3D12MA::ALLOCATION_DESC allocDescription = {};
    allocDescription.HeapType = D3D12_HEAP_TYPE_DEFAULT;

    switch (usage) {
    case Buffer::Usage::ConstantBuffer:
        initialResourceState = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        break;
    case Buffer::Usage::StorageBuffer:
        initialResourceState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        break;
    case Buffer::Usage::IndirectBuffer:
        initialResourceState = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        break;
    case Buffer::Usage::Vertex:
        initialResourceState = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        break;
    case Buffer::Usage::Index:
        initialResourceState = D3D12_RESOURCE_STATE_INDEX_BUFFER;
        break;
    case Buffer::Usage::RTInstanceBuffer:
        initialResourceState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        break;
    case Buffer::Usage::Upload:
        // "When you create a resource together with a D3D12_HEAP_TYPE_UPLOAD heap, you must set InitialResourceState to D3D12_RESOURCE_STATE_GENERIC_READ."
        // From: https://docs.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createcommittedresource
        initialResourceState = D3D12_RESOURCE_STATE_GENERIC_READ;
        allocDescription.HeapType = D3D12_HEAP_TYPE_UPLOAD; // try D3D12_HEAP_TYPE_GPU_UPLOAD!
        break;
    case Buffer::Usage::Readback:
        initialResourceState = D3D12_RESOURCE_STATE_COMMON; // ??
        allocDescription.HeapType = D3D12_HEAP_TYPE_READBACK;
        break;
    default:
        ASSERT_NOT_REACHED();
    }

    D3D12_RESOURCE_FLAGS resourceFlags = D3D12_RESOURCE_FLAG_NONE;
    if (storageCapable()) {
        resourceFlags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }

    if (usage == Buffer::Usage::ConstantBuffer) {
        // D3D12 ERROR: ID3D12Device::CreateConstantBufferView: Size of <...> is invalid. Device requires SizeInBytes be a multiple of 256. [ STATE_CREATION ERROR #650: CREATE_CONSTANT_BUFFER_VIEW_INVALID_DESC]
        constexpr size_t BufferMinimumAlignment = 256;
        m_sizeInMemory = ark::divideAndRoundUp<size_t>(m_size, BufferMinimumAlignment) * BufferMinimumAlignment;
    } else {
        m_sizeInMemory = m_size;
    }

    // TODO: Get rid of D3DX12!
    CD3DX12_RESOURCE_DESC bufferDescription = CD3DX12_RESOURCE_DESC::Buffer(m_sizeInMemory, resourceFlags);

    HRESULT hr = d3d12Backend.globalAllocator().CreateResource(
        &allocDescription, &bufferDescription,
        initialResourceState, NULL,
        bufferAllocation.GetAddressOf(),
        IID_PPV_ARGS(&bufferResource));

    if (FAILED(hr)) {
        ARKOSE_LOG(Fatal, "D3D12Buffer: could not create committed resource for buffer, exiting.");
    }

    resourceState = initialResourceState;
}

D3D12Buffer::~D3D12Buffer()
{
    //if (bufferAllocation != nullptr) {
    //    bufferAllocation->Release();
    //}
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

    switch (usage()) {
    case Buffer::Usage::Upload:
        if (!d3d12Backend.setBufferDataUsingMapping(*bufferResource.Get(), (uint8_t*)data, updateSize, offset)) {
            ARKOSE_LOG(Error, "Failed to update the data of transfer-optimal buffer.");
        }
        break;
    case Buffer::Usage::Readback:
        ARKOSE_LOG(Error, "Can't update buffer with Readback memory hint, ignoring.");
        break;
    default:
        if (!d3d12Backend.setBufferDataUsingStagingBuffer(*this, (uint8_t*)data, updateSize, offset)) {
            ARKOSE_LOG(Error, "Failed to update data of buffer");
        }
        break;
    }
}

void D3D12Buffer::reallocateWithSize(size_t newSize, ReallocateStrategy strategy)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();
    
    ASSERT_NOT_REACHED();
}
