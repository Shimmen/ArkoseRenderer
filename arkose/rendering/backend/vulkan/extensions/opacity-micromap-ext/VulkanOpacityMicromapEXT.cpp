#include "VulkanOpacityMicromapEXT.h"

#include "rendering/backend/vulkan/VulkanBackend.h"
#include "rendering/backend/vulkan/extensions/VulkanProcAddress.h"

VulkanOpacityMicromapEXT::VulkanOpacityMicromapEXT(VulkanBackend& backend, VkPhysicalDevice physicalDevice, VkDevice device)
    : m_backend(backend)
    , m_physicalDevice(physicalDevice)
    , m_device(device)
{
    SCOPED_PROFILE_ZONE_BACKEND();

    vkCreateMicromapEXT = FetchVulkanDeviceProcAddr(device, vkCreateMicromapEXT);
    vkDestroyMicromapEXT = FetchVulkanDeviceProcAddr(device, vkDestroyMicromapEXT);
    vkCmdBuildMicromapsEXT = FetchVulkanDeviceProcAddr(device, vkCmdBuildMicromapsEXT);
    vkBuildMicromapsEXT = FetchVulkanDeviceProcAddr(device, vkBuildMicromapsEXT);
    vkCopyMicromapEXT = FetchVulkanDeviceProcAddr(device, vkCopyMicromapEXT);
    vkCopyMicromapToMemoryEXT = FetchVulkanDeviceProcAddr(device, vkCopyMicromapToMemoryEXT);
    vkCopyMemoryToMicromapEXT = FetchVulkanDeviceProcAddr(device, vkCopyMemoryToMicromapEXT);
    vkWriteMicromapsPropertiesEXT = FetchVulkanDeviceProcAddr(device, vkWriteMicromapsPropertiesEXT);
    vkCmdCopyMicromapEXT = FetchVulkanDeviceProcAddr(device, vkCmdCopyMicromapEXT);
    vkCmdCopyMicromapToMemoryEXT = FetchVulkanDeviceProcAddr(device, vkCmdCopyMicromapToMemoryEXT);
    vkCmdCopyMemoryToMicromapEXT = FetchVulkanDeviceProcAddr(device, vkCmdCopyMemoryToMicromapEXT);
    vkCmdWriteMicromapsPropertiesEXT = FetchVulkanDeviceProcAddr(device, vkCmdWriteMicromapsPropertiesEXT);
    vkGetDeviceMicromapCompatibilityEXT = FetchVulkanDeviceProcAddr(device, vkGetDeviceMicromapCompatibilityEXT);
    vkGetMicromapBuildSizesEXT = FetchVulkanDeviceProcAddr(device, vkGetMicromapBuildSizesEXT);
}

VulkanOpacityMicromapEXT::~VulkanOpacityMicromapEXT()
{
}
