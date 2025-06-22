#pragma once

#include <vulkan/vulkan.h>

class VulkanBackend;

// Defines extension interface for
//  VK_EXT_debug_utils
class VulkanDebugUtils {
public:
    VulkanDebugUtils(VulkanBackend&, VkInstance);

    static VkDebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo();

    PFN_vkSetDebugUtilsObjectNameEXT vkSetDebugUtilsObjectNameEXT { nullptr };
    PFN_vkSetDebugUtilsObjectTagEXT vkSetDebugUtilsObjectTagEXT { nullptr };
    PFN_vkQueueBeginDebugUtilsLabelEXT vkQueueBeginDebugUtilsLabelEXT { nullptr };
    PFN_vkQueueEndDebugUtilsLabelEXT vkQueueEndDebugUtilsLabelEXT { nullptr };
    PFN_vkQueueInsertDebugUtilsLabelEXT vkQueueInsertDebugUtilsLabelEXT { nullptr };
    PFN_vkCmdBeginDebugUtilsLabelEXT vkCmdBeginDebugUtilsLabelEXT { nullptr };
    PFN_vkCmdEndDebugUtilsLabelEXT vkCmdEndDebugUtilsLabelEXT { nullptr };
    PFN_vkCmdInsertDebugUtilsLabelEXT vkCmdInsertDebugUtilsLabelEXT { nullptr };
    PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT { nullptr };
    PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT { nullptr };
    PFN_vkSubmitDebugUtilsMessageEXT vkSubmitDebugUtilsMessageEXT { nullptr };

private:
    VulkanBackend& m_backend;
    VkInstance m_instance;

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugMessageCallback(VkDebugUtilsMessageSeverityFlagBitsEXT,
                                                               VkDebugUtilsMessageTypeFlagsEXT,
                                                               VkDebugUtilsMessengerCallbackDataEXT const*,
                                                               void* pUserData);
};
