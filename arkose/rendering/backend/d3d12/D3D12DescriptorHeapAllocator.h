#pragma once

#include "core/Types.h"
#include "rendering/backend/d3d12/D3D12Common.h"
#include <offsetAllocator.hpp>

struct D3D12DescriptorAllocation {
    D3D12_CPU_DESCRIPTOR_HANDLE firstCpuDescriptor {};
    D3D12_GPU_DESCRIPTOR_HANDLE firstGpuDescriptor {}; // invalid if `shaderVisible` is false

    u32 count { 0 };
    bool shaderVisible{ false };

    OffsetAllocator::Allocation internalAllocation {};

    bool valid() const { return count > 0 && internalAllocation.offset != OffsetAllocator::Allocation::NO_SPACE; }
};

class D3D12DescriptorHeapAllocator {
public:
    D3D12DescriptorHeapAllocator(ID3D12Device& device, D3D12_DESCRIPTOR_HEAP_TYPE heapType, bool shaderVisible, u32 descriptorCount);
    ~D3D12DescriptorHeapAllocator();

    D3D12DescriptorHeapAllocator(D3D12DescriptorHeapAllocator&) = delete;
    D3D12DescriptorHeapAllocator& operator=(D3D12DescriptorHeapAllocator&) = delete;

    D3D12DescriptorAllocation allocate(u32 count);
    void free(D3D12DescriptorAllocation&);

    ID3D12DescriptorHeap const* heap() const { return m_descriptorHeap.Get(); }
    ID3D12DescriptorHeap* heap() { return m_descriptorHeap.Get(); }

private:
    OffsetAllocator::Allocator m_allocator;

    ComPtr<ID3D12DescriptorHeap> m_descriptorHeap { nullptr };
    size_t m_descriptorHandleIncrementSize { 0 };

    bool m_shaderVisible{ false };

    D3D12_CPU_DESCRIPTOR_HANDLE m_firstCpuDescriptor {};
    D3D12_GPU_DESCRIPTOR_HANDLE m_firstGpuDescriptor {};
};
