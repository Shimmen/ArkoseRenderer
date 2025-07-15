#pragma once

#include <vulkan/vulkan.h>

class VulkanBackend;

// Defines extension interface for
//  VK_EXT_opacity_micromap
class VulkanOpacityMicromapEXT {
public:
    VulkanOpacityMicromapEXT(VulkanBackend&, VkPhysicalDevice, VkDevice);
    ~VulkanOpacityMicromapEXT();

    // API
    PFN_vkCreateMicromapEXT vkCreateMicromapEXT { nullptr };
    PFN_vkDestroyMicromapEXT vkDestroyMicromapEXT { nullptr };
    PFN_vkCmdBuildMicromapsEXT vkCmdBuildMicromapsEXT { nullptr };
    PFN_vkBuildMicromapsEXT vkBuildMicromapsEXT { nullptr };
    PFN_vkCopyMicromapEXT vkCopyMicromapEXT { nullptr };
    PFN_vkCopyMicromapToMemoryEXT vkCopyMicromapToMemoryEXT { nullptr };
    PFN_vkCopyMemoryToMicromapEXT vkCopyMemoryToMicromapEXT { nullptr };
    PFN_vkWriteMicromapsPropertiesEXT vkWriteMicromapsPropertiesEXT { nullptr };
    PFN_vkCmdCopyMicromapEXT vkCmdCopyMicromapEXT { nullptr };
    PFN_vkCmdCopyMicromapToMemoryEXT vkCmdCopyMicromapToMemoryEXT { nullptr };
    PFN_vkCmdCopyMemoryToMicromapEXT vkCmdCopyMemoryToMicromapEXT { nullptr };
    PFN_vkCmdWriteMicromapsPropertiesEXT vkCmdWriteMicromapsPropertiesEXT { nullptr };
    PFN_vkGetDeviceMicromapCompatibilityEXT vkGetDeviceMicromapCompatibilityEXT { nullptr };
    PFN_vkGetMicromapBuildSizesEXT vkGetMicromapBuildSizesEXT { nullptr };

    // Helpers
    const VkPhysicalDeviceOpacityMicromapPropertiesEXT& opacityMicromapProperties() const { return m_opacityMicromapProperties; }

private:
    VulkanBackend& m_backend;
    VkPhysicalDevice m_physicalDevice;
    VkDevice m_device;

    VkPhysicalDeviceOpacityMicromapPropertiesEXT m_opacityMicromapProperties {};
};
