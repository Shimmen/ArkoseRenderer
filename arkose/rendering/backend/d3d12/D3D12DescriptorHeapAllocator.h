#pragma once

#include "core/Types.h"
#include "rendering/backend/d3d12/D3D12Common.h"
#include <offsetAllocator.hpp>

struct D3D12DescriptorAllocation {
    D3D12_CPU_DESCRIPTOR_HANDLE firstCpuDescriptor {};
    u32 count { 0 };

    OffsetAllocator::Allocation internalAllocation {};
};

class D3D12DescriptorHeapAllocator {
public:
    D3D12DescriptorHeapAllocator(ID3D12Device& device, D3D12_DESCRIPTOR_HEAP_TYPE heapType, u32 descriptorCount);
    ~D3D12DescriptorHeapAllocator();

    D3D12DescriptorHeapAllocator(D3D12DescriptorHeapAllocator&) = delete;
    D3D12DescriptorHeapAllocator& operator=(D3D12DescriptorHeapAllocator&) = delete;

    D3D12DescriptorAllocation allocate(u32 count);
    void free(D3D12DescriptorAllocation&);

private:
    OffsetAllocator::Allocator m_allocator;

    ComPtr<ID3D12DescriptorHeap> m_descriptorHeap { nullptr };
    size_t m_descriptorHandleIncrementSize { 0 };

    D3D12_CPU_DESCRIPTOR_HANDLE m_firstCpuDescriptor {};
};
