#pragma once

#include "rendering/backend/base/AccelerationStructure.h"

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>
#include <memory>

struct VulkanTopLevelASNV final : public TopLevelAS {
public:
    VulkanTopLevelASNV(Backend&, uint32_t maxInstanceCount, std::vector<RTGeometryInstance>);
    virtual ~VulkanTopLevelASNV() override;

    virtual void setName(const std::string& name) override;

    void build(VkCommandBuffer, AccelerationStructureBuildType);

    void updateInstanceDataWithUploadBuffer(const std::vector<RTGeometryInstance>&, UploadBuffer&) override;

    VkAccelerationStructureNV accelerationStructure;
    VmaAllocation allocation { VK_NULL_HANDLE };
    uint64_t handle { 0u };

    VkBuildAccelerationStructureFlagsNV accelerationStructureFlags { 0u };

    std::unique_ptr<Buffer> instanceBuffer {};
};

struct VulkanBottomLevelASNV final : public BottomLevelAS {
public:
    VulkanBottomLevelASNV(Backend&, std::vector<RTGeometry>);
    virtual ~VulkanBottomLevelASNV() override;

    virtual void setName(const std::string& name) override;

    VkAccelerationStructureNV accelerationStructure;
    VmaAllocation allocation { VK_NULL_HANDLE };
    uint64_t handle { 0u };

    std::vector<std::pair<VkBuffer, VmaAllocation>> associatedBuffers;
};
