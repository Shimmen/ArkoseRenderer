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

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugReportCallback(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location,
                                                              int32_t messageCode, const char* pLayerPrefix, const char* pMessage, void* pUserData);

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

    // Technically part of the VK_EXT_debug_report extention (this file is for VK_EXT_debug_utils) but I'll include them here for convenience
    PFN_vkCreateDebugReportCallbackEXT vkCreateDebugReportCallbackEXT { nullptr };
    PFN_vkDestroyDebugReportCallbackEXT vkDestroyDebugReportCallbackEXT { nullptr };
    PFN_vkDebugReportMessageEXT vkDebugReportMessageEXT { nullptr }; 

private:
    VulkanBackend& m_backend;
    VkInstance m_instance;
};
