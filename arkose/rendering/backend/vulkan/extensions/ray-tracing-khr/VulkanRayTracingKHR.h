#pragma once

#include <ark/matrix.h>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

class VulkanBackend;

// Defines extension interface for
//  1. VK_KHR_acceleration_structure
//  2. VK_KHR_ray_tracing_pipeline
//  3. VK_KHR_ray_query
class VulkanRayTracingKHR {
public:
    VulkanRayTracingKHR(VulkanBackend&, VkPhysicalDevice, VkDevice);
    ~VulkanRayTracingKHR();

    // VK_KHR_acceleration_structure
    PFN_vkBuildAccelerationStructuresKHR vkBuildAccelerationStructuresKHR { nullptr };
    PFN_vkCmdBuildAccelerationStructuresIndirectKHR vkCmdBuildAccelerationStructuresIndirectKHR { nullptr };
    PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR { nullptr };
    PFN_vkCmdCopyAccelerationStructureKHR vkCmdCopyAccelerationStructureKHR { nullptr };
    PFN_vkCmdCopyAccelerationStructureToMemoryKHR vkCmdCopyAccelerationStructureToMemoryKHR { nullptr };
    PFN_vkCmdCopyMemoryToAccelerationStructureKHR vkCmdCopyMemoryToAccelerationStructureKHR { nullptr };
    PFN_vkCmdWriteAccelerationStructuresPropertiesKHR vkCmdWriteAccelerationStructuresPropertiesKHR { nullptr };
    PFN_vkCopyAccelerationStructureKHR vkCopyAccelerationStructureKHR { nullptr };
    PFN_vkCopyAccelerationStructureToMemoryKHR vkCopyAccelerationStructureToMemoryKHR { nullptr };
    PFN_vkCopyMemoryToAccelerationStructureKHR vkCopyMemoryToAccelerationStructureKHR { nullptr };
    PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR { nullptr };
    PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR { nullptr };
    PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR { nullptr };
    PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR { nullptr };
    PFN_vkGetDeviceAccelerationStructureCompatibilityKHR vkGetDeviceAccelerationStructureCompatibilityKHR { nullptr };
    PFN_vkWriteAccelerationStructuresPropertiesKHR vkWriteAccelerationStructuresPropertiesKHR { nullptr };

    // VK_KHR_ray_tracing_pipeline
    PFN_vkCmdSetRayTracingPipelineStackSizeKHR vkCmdSetRayTracingPipelineStackSizeKHR { nullptr };
    PFN_vkCmdTraceRaysIndirectKHR vkCmdTraceRaysIndirectKHR { nullptr };
    PFN_vkCmdTraceRaysKHR vkCmdTraceRaysKHR { nullptr };
    PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelinesKHR { nullptr };
    PFN_vkGetRayTracingCaptureReplayShaderGroupHandlesKHR vkGetRayTracingCaptureReplayShaderGroupHandlesKHR { nullptr };
    PFN_vkGetRayTracingShaderGroupHandlesKHR vkGetRayTracingShaderGroupHandlesKHR { nullptr };
    PFN_vkGetRayTracingShaderGroupStackSizeKHR vkGetRayTracingShaderGroupStackSizeKHR { nullptr };

    // Helpers

    std::pair<VkBuffer, VmaAllocation> createAccelerationStructureBuffer(VkDeviceSize size, bool deviceOnlyMemory, bool readOnlyMemory);
    VkTransformMatrixKHR toVkTransformMatrixKHR(mat4) const;

    const VkPhysicalDeviceAccelerationStructurePropertiesKHR& accelerationStructureProperties() const { return m_accelerationStructureProperties; }
    const VkPhysicalDeviceRayTracingPipelinePropertiesKHR& pipelineStateProperties() const { return m_rayTracingPipelineProperties; }

private:
    VulkanBackend& m_backend;
    VkPhysicalDevice m_physicalDevice;
    VkDevice m_device;

    VkPhysicalDeviceAccelerationStructurePropertiesKHR m_accelerationStructureProperties {};
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_rayTracingPipelineProperties {};
};
