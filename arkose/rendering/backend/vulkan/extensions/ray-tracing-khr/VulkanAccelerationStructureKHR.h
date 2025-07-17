#pragma once

#include "rendering/backend/base/AccelerationStructure.h"

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>
#include <memory>

struct VulkanTopLevelASKHR final : public TopLevelAS {
public:
    VulkanTopLevelASKHR(Backend&, uint32_t maxInstanceCount);
    virtual ~VulkanTopLevelASKHR() override;

    virtual void setName(const std::string& name) override;

    void build(VkCommandBuffer, AccelerationStructureBuildType);

    void updateInstanceDataWithUploadBuffer(const std::vector<RTGeometryInstance>&, UploadBuffer&) override;

    std::vector<VkAccelerationStructureInstanceKHR> createInstanceData(const std::vector<RTGeometryInstance>&) const;

    VkAccelerationStructureKHR accelerationStructure;
    VkDeviceAddress accelerationStructureDeviceAddress;
    VkDeviceAddress scratchBufferAddress;

    VkBuildAccelerationStructureFlagsKHR accelerationStructureFlags { 0u };

    std::pair<VkBuffer, VmaAllocation> accelerationStructureBufferAndAllocation {};
    std::pair<VkBuffer, VmaAllocation> scratchBufferAndAllocation {};
    std::unique_ptr<Buffer> instanceBuffer {};
};

struct VulkanBottomLevelASKHR final : public BottomLevelAS {
public:
    VulkanBottomLevelASKHR(Backend&, std::vector<RTGeometry>);
    virtual ~VulkanBottomLevelASKHR() override;

    virtual void setName(const std::string& name) override;

    void build(VkCommandBuffer, AccelerationStructureBuildType);
    void copyFrom(VkCommandBuffer, VulkanBottomLevelASKHR const&);
    bool compact(VkCommandBuffer);

    VkAccelerationStructureKHR accelerationStructure;
    VkDeviceAddress accelerationStructureDeviceAddress;
    VkDeviceAddress scratchBufferAddress;

    std::pair<VkBuffer, VmaAllocation> blasBufferAndAllocation {};
    std::pair<VkBuffer, VmaAllocation> scratchBufferAndAllocation {};
    std::pair<VkBuffer, VmaAllocation> transformBufferAndAllocation {};

    // Store for rebuilding purposes
    std::vector<VkAccelerationStructureGeometryKHR> vkGeometries {};
    VkAccelerationStructureBuildGeometryInfoKHR previewBuildInfo {};
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> rangeInfos {};

    enum class CompactionState {
        NotCompacted,
        CompactSizeRequested,
        Compacted,
    };

    CompactionState compactionState { CompactionState::NotCompacted };
    VkQueryPool compactionQueryPool {};
};
