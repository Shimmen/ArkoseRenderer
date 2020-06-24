#pragma once

#include "backend/vulkan/VulkanResources.h"
#include <mooslib/matrix.h>
#include <optional>
#include <vulkan/vulkan.h>

class VulkanRTX {
public:
    VulkanRTX(VulkanBackend&, VkPhysicalDevice, VkDevice);

    static bool isSupportedOnPhysicalDevice(VkPhysicalDevice);

    PFN_vkCreateAccelerationStructureNV vkCreateAccelerationStructureNV { nullptr };
    PFN_vkDestroyAccelerationStructureNV vkDestroyAccelerationStructureNV { nullptr };
    PFN_vkBindAccelerationStructureMemoryNV vkBindAccelerationStructureMemoryNV { nullptr };
    PFN_vkGetAccelerationStructureHandleNV vkGetAccelerationStructureHandleNV { nullptr };
    PFN_vkGetAccelerationStructureMemoryRequirementsNV vkGetAccelerationStructureMemoryRequirementsNV { nullptr };
    PFN_vkCmdBuildAccelerationStructureNV vkCmdBuildAccelerationStructureNV { nullptr };
    PFN_vkCreateRayTracingPipelinesNV vkCreateRayTracingPipelinesNV { nullptr };
    PFN_vkGetRayTracingShaderGroupHandlesNV vkGetRayTracingShaderGroupHandlesNV { nullptr };
    PFN_vkCmdTraceRaysNV vkCmdTraceRaysNV { nullptr };

    const VkPhysicalDeviceRayTracingPropertiesNV& properties() const;

    VkBuffer createInstanceBuffer(const std::vector<RTGeometryInstance>&, VmaAllocation&) const;
    VkBuffer createScratchBufferForAccelerationStructure(VkAccelerationStructureNV, bool updateInPlace, VmaAllocation&) const;

public:
    struct GeometryInstance {
        moos::mat3x4 transform;
        uint32_t instanceId : 24;
        uint32_t mask : 8;
        uint32_t instanceOffset : 24;
        uint32_t flags : 8;
        uint64_t accelerationStructureHandle;
    };

private:
    VulkanBackend& m_backend;
    VkPhysicalDevice m_physicalDevice;
    VkDevice m_device;

    VkPhysicalDeviceRayTracingPropertiesNV m_rayTracingProperties { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PROPERTIES_NV };
};
