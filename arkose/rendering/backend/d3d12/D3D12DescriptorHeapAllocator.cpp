#include "D3D12DescriptorHeapAllocator.h"

D3D12DescriptorHeapAllocator::D3D12DescriptorHeapAllocator(ID3D12Device& device, D3D12_DESCRIPTOR_HEAP_TYPE heapType, bool shaderVisible, u32 descriptorCount)
    : m_allocator(descriptorCount)
    , m_shaderVisible(shaderVisible)
{
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Type = heapType;
    heapDesc.NumDescriptors = descriptorCount;

    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (shaderVisible) {
        heapDesc.Flags |= D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    }

    heapDesc.NodeMask = 0;

    if (auto hr = device.CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_descriptorHeap)); !SUCCEEDED(hr)) {
        ARKOSE_LOG(Fatal, "D3D12DescriptorHeapAllocator: failed to create descriptor heap, exiting.");
    }

    m_descriptorHandleIncrementSize = device.GetDescriptorHandleIncrementSize(heapType);

    m_firstCpuDescriptor = m_descriptorHeap->GetCPUDescriptorHandleForHeapStart();
    if (shaderVisible) {
        m_firstGpuDescriptor = m_descriptorHeap->GetGPUDescriptorHandleForHeapStart();
    }
}

D3D12DescriptorHeapAllocator::~D3D12DescriptorHeapAllocator()
{
}

D3D12DescriptorAllocation D3D12DescriptorHeapAllocator::allocate(u32 count)
{
    OffsetAllocator::Allocation internalAllocation = m_allocator.allocate(count);

    // TODO: Handle errors!
    ARKOSE_ASSERT(internalAllocation.offset != OffsetAllocator::Allocation::NO_SPACE);

    D3D12DescriptorAllocation allocation;
    allocation.internalAllocation = internalAllocation;

    allocation.count = count;
    allocation.shaderVisible = m_shaderVisible;

    allocation.firstCpuDescriptor = m_firstCpuDescriptor;
    allocation.firstCpuDescriptor.ptr += internalAllocation.offset * m_descriptorHandleIncrementSize;

    if ( m_shaderVisible ) {
        allocation.firstGpuDescriptor = m_firstGpuDescriptor;
        allocation.firstGpuDescriptor.ptr += internalAllocation.offset * m_descriptorHandleIncrementSize;
    }

    return allocation;
}

void D3D12DescriptorHeapAllocator::free(D3D12DescriptorAllocation& allocation)
{
    // TODO: Handle errors!
    ARKOSE_ASSERT(allocation.internalAllocation.offset != OffsetAllocator::Allocation::NO_SPACE);

    m_allocator.free(allocation.internalAllocation);

    // Invalidate the descriptors to hopefully avoid use-after-free
    allocation.count = 0;
    allocation.firstCpuDescriptor.ptr = 0;
    if (allocation.shaderVisible) {
        allocation.firstGpuDescriptor.ptr = 0;
    }
}
