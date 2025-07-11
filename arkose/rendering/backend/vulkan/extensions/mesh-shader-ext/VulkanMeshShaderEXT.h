#pragma once

#include <vulkan/vulkan.h>

class VulkanBackend;

// Defines extension interface for
//  VK_EXT_mesh_shader
class VulkanMeshShaderEXT {
public:
    VulkanMeshShaderEXT(VulkanBackend&, VkPhysicalDevice, VkDevice);
    ~VulkanMeshShaderEXT();

    // API
    PFN_vkCmdDrawMeshTasksEXT vkCmdDrawMeshTasksEXT { nullptr };
    PFN_vkCmdDrawMeshTasksIndirectEXT vkCmdDrawMeshTasksIndirectEXT { nullptr };
    PFN_vkCmdDrawMeshTasksIndirectCountEXT vkCmdDrawMeshTasksIndirectCountEXT { nullptr };

    // Helpers
    const VkPhysicalDeviceMeshShaderPropertiesEXT& meshShaderProperties() const { return m_meshShaderProperties; }

private:
    VulkanBackend& m_backend;
    VkPhysicalDevice m_physicalDevice;
    VkDevice m_device;

    VkPhysicalDeviceMeshShaderPropertiesEXT m_meshShaderProperties {};
};