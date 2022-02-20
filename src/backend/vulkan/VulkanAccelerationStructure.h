#pragma once

#include "backend/base/AccelerationStructure.h"

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

struct VulkanTopLevelAS final : public TopLevelAS {
public:
    VulkanTopLevelAS(Backend&, uint32_t maxInstanceCount, std::vector<RTGeometryInstance>);
    virtual ~VulkanTopLevelAS() override;

    virtual void setName(const std::string& name) override;

    enum class BuildType {
        BuildInitial,
        UpdateInPlace,
    };

    void build(VkCommandBuffer, BuildType);

    void updateInstanceDataWithUploadBuffer(const std::vector<RTGeometryInstance>&, UploadBuffer&) override;

    VkAccelerationStructureNV accelerationStructure;
    VkDeviceMemory memory { VK_NULL_HANDLE };
    uint64_t handle { 0u };

    VkBuildAccelerationStructureFlagsNV accelerationStructureFlags { 0u };

    std::unique_ptr<Buffer> instanceBuffer {};
};

struct VulkanBottomLevelAS final : public BottomLevelAS {
public:
    VulkanBottomLevelAS(Backend&, std::vector<RTGeometry>);
    virtual ~VulkanBottomLevelAS() override;

    virtual void setName(const std::string& name) override;

    VkAccelerationStructureNV accelerationStructure;
    VkDeviceMemory memory { VK_NULL_HANDLE };
    uint64_t handle { 0u };

    std::vector<std::pair<VkBuffer, VmaAllocation>> associatedBuffers;
};
