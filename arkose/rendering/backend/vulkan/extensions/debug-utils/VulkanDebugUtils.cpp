#include "VulkanDebugUtils.h"

#include "rendering/backend/vulkan/VulkanBackend.h"
#include "core/Logging.h"

VulkanDebugUtils::VulkanDebugUtils(VulkanBackend& backend, VkInstance instance)
    : m_backend(backend)
    , m_instance(instance)
{
    vkSetDebugUtilsObjectNameEXT = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(vkGetInstanceProcAddr(m_instance, "vkSetDebugUtilsObjectNameEXT"));
    vkSetDebugUtilsObjectTagEXT = reinterpret_cast<PFN_vkSetDebugUtilsObjectTagEXT>(vkGetInstanceProcAddr(m_instance, "vkSetDebugUtilsObjectTagEXT"));
    vkQueueBeginDebugUtilsLabelEXT = reinterpret_cast<PFN_vkQueueBeginDebugUtilsLabelEXT>(vkGetInstanceProcAddr(m_instance, "vkQueueBeginDebugUtilsLabelEXT"));
    vkQueueEndDebugUtilsLabelEXT = reinterpret_cast<PFN_vkQueueEndDebugUtilsLabelEXT>(vkGetInstanceProcAddr(m_instance, "vkQueueEndDebugUtilsLabelEXT"));
    vkQueueInsertDebugUtilsLabelEXT = reinterpret_cast<PFN_vkQueueInsertDebugUtilsLabelEXT>(vkGetInstanceProcAddr(m_instance, "vkQueueInsertDebugUtilsLabelEXT"));
    vkCmdBeginDebugUtilsLabelEXT = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(vkGetInstanceProcAddr(m_instance, "vkCmdBeginDebugUtilsLabelEXT"));
    vkCmdEndDebugUtilsLabelEXT = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(vkGetInstanceProcAddr(m_instance, "vkCmdEndDebugUtilsLabelEXT"));
    vkCmdInsertDebugUtilsLabelEXT = reinterpret_cast<PFN_vkCmdInsertDebugUtilsLabelEXT>(vkGetInstanceProcAddr(m_instance, "vkCmdInsertDebugUtilsLabelEXT"));
    vkCreateDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT"));
    vkDestroyDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT"));
    vkSubmitDebugUtilsMessageEXT = reinterpret_cast<PFN_vkSubmitDebugUtilsMessageEXT>(vkGetInstanceProcAddr(m_instance, "vkSubmitDebugUtilsMessageEXT"));
}

VkBool32 VulkanDebugUtils::debugMessageCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                VkDebugUtilsMessageTypeFlagsEXT messageType,
                                                VkDebugUtilsMessengerCallbackDataEXT const* pCallbackData,
                                                void* pUserData)
{
    if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
        switch (messageSeverity) {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
            ARKOSE_ERROR("Vulkan validation error: ({}) {}", pCallbackData->pMessageIdName, pCallbackData->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
            ARKOSE_ERROR("Vulkan validation warning: ({}) {}", pCallbackData->pMessageIdName, pCallbackData->pMessage);
            break;
        default:
            // I don't think there are validation 'info' or 'verbose' messages..?
            ASSERT_NOT_REACHED();
        }
    } else {
        switch (messageSeverity) {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
            ARKOSE_LOG(Error, "Vulkan debug message; {}", pCallbackData->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
            ARKOSE_LOG(Warning, "Vulkan debug message; {}", pCallbackData->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
            ARKOSE_LOG(Info, "Vulkan debug message; {}", pCallbackData->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
            ARKOSE_LOG(Verbose, "Vulkan debug message; {}", pCallbackData->pMessage);
            break;
        default:
            ASSERT_NOT_REACHED();
        }
    }

    return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT VulkanDebugUtils::debugMessengerCreateInfo()
{
    VkDebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
    debugMessengerCreateInfo.pfnUserCallback = debugMessageCallback;
    debugMessengerCreateInfo.pUserData = nullptr;

    debugMessengerCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT; // NOLINT(hicpp-signed-bitwise)
    if (vulkanVerboseDebugMessages) {
        debugMessengerCreateInfo.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT;
    }
    debugMessengerCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT; // NOLINT(hicpp-signed-bitwise)

    return debugMessengerCreateInfo;
}
