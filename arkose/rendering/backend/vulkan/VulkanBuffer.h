#pragma once

#include "rendering/backend/base/Buffer.h"

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

struct VulkanBuffer final : public Buffer {
public:
    VulkanBuffer(Backend&, size_t size, Usage);
    virtual ~VulkanBuffer() override;

    virtual void setName(const std::string& name) override;

    bool mapData(MapMode, size_t size, size_t offset, std::function<void(std::byte*)>&& mapCallback) override;

    void updateData(const std::byte* data, size_t size, size_t offset) override;
    void reallocateWithSize(size_t newSize, ReallocateStrategy) override;

    VkBuffer buffer;
    VmaAllocation allocation;
    VmaAllocationInfo allocationInfo;

private:
    void createInternal(size_t size, VkBuffer& buffer, VmaAllocation& allocation, VmaAllocationInfo& allocationInfo);
    void destroyInternal(VkBuffer buffer, VmaAllocation allocation, VmaAllocationInfo& allocationInfo);
};
