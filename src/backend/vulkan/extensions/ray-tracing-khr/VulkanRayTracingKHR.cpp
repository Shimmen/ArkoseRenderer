#include "VulkanRayTracingKHR.h"

#include "backend/vulkan/VulkanBackend.h"
#include "utility/Logging.h"

VulkanRayTracingKHR::VulkanRayTracingKHR(VulkanBackend& backend, VkPhysicalDevice physicalDevice, VkDevice device)
    : m_backend(backend)
    , m_physicalDevice(physicalDevice)
    , m_device(device)
{
    SCOPED_PROFILE_ZONE_BACKEND();

    VkPhysicalDeviceProperties2 deviceProps2 { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
    deviceProps2.pNext = &m_accelerationStructureProperties;
    m_accelerationStructureProperties.pNext = &m_rayTracingPipelineProperties;
    vkGetPhysicalDeviceProperties2(m_physicalDevice, &deviceProps2);

    #define FetchProcAddr(function) function = reinterpret_cast<PFN_##function>(vkGetDeviceProcAddr(m_device, #function))
    {
        // VK_KHR_acceleration_structure
        FetchProcAddr(vkBuildAccelerationStructuresKHR);
        FetchProcAddr(vkCmdBuildAccelerationStructuresIndirectKHR);
        FetchProcAddr(vkCmdBuildAccelerationStructuresKHR);
        FetchProcAddr(vkCmdCopyAccelerationStructureKHR);
        FetchProcAddr(vkCmdCopyAccelerationStructureToMemoryKHR);
        FetchProcAddr(vkCmdCopyMemoryToAccelerationStructureKHR);
        FetchProcAddr(vkCreateAccelerationStructureKHR);
        FetchProcAddr(vkDestroyAccelerationStructureKHR);
        FetchProcAddr(vkGetAccelerationStructureBuildSizesKHR);
        FetchProcAddr(vkGetAccelerationStructureDeviceAddressKHR);
        FetchProcAddr(vkGetDeviceAccelerationStructureCompatibilityKHR);
        FetchProcAddr(vkWriteAccelerationStructuresPropertiesKHR);

        // VK_KHR_ray_tracing_pipeline
        FetchProcAddr(vkCmdSetRayTracingPipelineStackSizeKHR);
        FetchProcAddr(vkCmdTraceRaysIndirectKHR);
        FetchProcAddr(vkCmdTraceRaysKHR);
        FetchProcAddr(vkCreateRayTracingPipelinesKHR);
        FetchProcAddr(vkGetRayTracingCaptureReplayShaderGroupHandlesKHR);
        FetchProcAddr(vkGetRayTracingShaderGroupHandlesKHR);
        FetchProcAddr(vkGetRayTracingShaderGroupStackSizeKHR);

        // VK_KHR_ray_query
        // ... are all in-shader
    }
    #undef FetchProcAddr

    // Create shared buffer
    {
        m_sharedScratchBuffer = createAccelerationStructureBuffer(VulkanRayTracingKHR::SharedScratchBufferSize, true, false);

        VkBufferDeviceAddressInfo bufferDeviceAddressInfo { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
        bufferDeviceAddressInfo.buffer = m_sharedScratchBuffer.first;
        m_sharedScratchBufferAddress = vkGetBufferDeviceAddress(m_device, &bufferDeviceAddressInfo);
    }
}

VulkanRayTracingKHR::~VulkanRayTracingKHR()
{
    vmaDestroyBuffer(m_backend.globalAllocator(), m_sharedScratchBuffer.first, m_sharedScratchBuffer.second);
}

std::pair<VkBuffer, VmaAllocation> VulkanRayTracingKHR::createAccelerationStructureBuffer(VkDeviceSize size, bool deviceOnlyMemory, bool readOnlyMemory)
{
    SCOPED_PROFILE_ZONE_BACKEND();

    VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufferCreateInfo.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    bufferCreateInfo.size = size;

    bufferCreateInfo.usage |= readOnlyMemory
        ? VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
        : VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;

    if (vulkanDebugMode) {
        // for nsight debugging & similar stuff)
        bufferCreateInfo.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }

    VmaAllocationCreateInfo scratchAllocCreateInfo = {};
    scratchAllocCreateInfo.usage = deviceOnlyMemory
        ? VMA_MEMORY_USAGE_GPU_ONLY
        : VMA_MEMORY_USAGE_CPU_TO_GPU;

    VkBuffer buffer;
    VmaAllocation allocation;
    if (vmaCreateBuffer(m_backend.globalAllocator(), &bufferCreateInfo, &scratchAllocCreateInfo, &buffer, &allocation, nullptr) != VK_SUCCESS) {
        LogErrorAndExit("Vulkan ray tracing: could not create acceleration structure buffer.\n");
    }

    return { buffer, allocation };
}

VkTransformMatrixKHR VulkanRayTracingKHR::toVkTransformMatrixKHR(mat4 inMatrix) const
{
    VkTransformMatrixKHR matrix;

    for (int row = 0; row < 3; ++row) {
        matrix.matrix[row][0] = inMatrix[0][row];
        matrix.matrix[row][1] = inMatrix[1][row];
        matrix.matrix[row][2] = inMatrix[2][row];
        matrix.matrix[row][3] = inMatrix[3][row];
    }

    return matrix;
}