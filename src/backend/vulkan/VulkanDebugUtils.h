#pragma once

#include "backend/vulkan/VulkanResources.h"
#include <vulkan/vulkan.h>

class VulkanBackend;

class VulkanDebugUtils {
public:
    VulkanDebugUtils(VulkanBackend&, VkInstance);

    static VkDebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo();
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugMessageCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageTypes,
                                                               const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData);

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
};
