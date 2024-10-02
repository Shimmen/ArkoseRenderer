#include "VulkanMeshShaderEXT.h"

#include "rendering/backend/vulkan/extensions/VulkanProcAddress.h"

VulkanMeshShaderEXT::VulkanMeshShaderEXT(VulkanBackend& backend, VkPhysicalDevice physicalDevice, VkDevice device)
    : m_backend(backend)
    , m_physicalDevice(physicalDevice)
    , m_device(device)
{
    SCOPED_PROFILE_ZONE_BACKEND();

    m_meshShaderProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT;

    VkPhysicalDeviceProperties2 deviceProps2 { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
    deviceProps2.pNext = &m_meshShaderProperties;
    vkGetPhysicalDeviceProperties2(m_physicalDevice, &deviceProps2);

    vkCmdDrawMeshTasksEXT = FetchVulkanDeviceProcAddr(device, vkCmdDrawMeshTasksEXT);
    vkCmdDrawMeshTasksIndirectEXT = FetchVulkanDeviceProcAddr(device, vkCmdDrawMeshTasksIndirectEXT);
    vkCmdDrawMeshTasksIndirectCountEXT = FetchVulkanDeviceProcAddr(device, vkCmdDrawMeshTasksIndirectCountEXT);

}

VulkanMeshShaderEXT::~VulkanMeshShaderEXT()
{

}
