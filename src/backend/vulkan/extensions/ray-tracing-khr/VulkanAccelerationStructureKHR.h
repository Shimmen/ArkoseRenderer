#pragma once

#include "backend/base/AccelerationStructure.h"

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

struct VulkanTopLevelASKHR final : public TopLevelAS {
public:
    VulkanTopLevelASKHR(Backend&, uint32_t maxInstanceCount, std::vector<RTGeometryInstance>);
    virtual ~VulkanTopLevelASKHR() override;

    virtual void setName(const std::string& name) override;

    void build(VkCommandBuffer, AccelerationStructureBuildType);

    void updateInstanceDataWithUploadBuffer(const std::vector<RTGeometryInstance>&, UploadBuffer&) override;

    std::vector<VkAccelerationStructureInstanceKHR> createInstanceData(const std::vector<RTGeometryInstance>&) const;

    VkAccelerationStructureKHR accelerationStructure;
    VkDeviceAddress accelerationStructureDeviceAddress;

    VkBuildAccelerationStructureFlagsKHR accelerationStructureFlags { 0u };

    std::pair<VkBuffer, VmaAllocation> accelerationStructureBufferAndAllocation {};
    std::unique_ptr<Buffer> instanceBuffer {};
};

struct VulkanBottomLevelASKHR final : public BottomLevelAS {
public:
    VulkanBottomLevelASKHR(Backend&, std::vector<RTGeometry>);
    virtual ~VulkanBottomLevelASKHR() override;

    virtual void setName(const std::string& name) override;

    VkAccelerationStructureKHR accelerationStructure;
    VkDeviceAddress accelerationStructureDeviceAddress;

    std::vector<std::pair<VkBuffer, VmaAllocation>> associatedBuffers;
};
