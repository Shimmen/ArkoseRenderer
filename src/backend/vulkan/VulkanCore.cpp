#include "VulkanCore.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "utility/GlobalState.h"
#include "utility/Logging.h"
#include <algorithm>
#include <unordered_set>

VulkanCore::VulkanCore(GLFWwindow* window, bool debugModeEnabled)
    : m_window(window)
    , m_debugModeEnabled(debugModeEnabled)
{
    if (debugModeEnabled) {

        LogInfo("VulkanCore::VulkanCore(): debug mode enabled!\n");

        m_activeValidationLayers.emplace_back("VK_LAYER_KHRONOS_validation");
        m_activeValidationLayers.emplace_back("VK_LAYER_LUNARG_standard_validation");

        auto dbgMessengerCreateInfo = debugMessengerCreateInfo();
        m_instance = createInstance(&dbgMessengerCreateInfo);
        m_messenger = createDebugMessenger(m_instance, &dbgMessengerCreateInfo);

    } else {
        m_instance = createInstance(nullptr);
    }

    if (!verifyValidationLayerSupport()) {
        LogErrorAndExit("VulkanCore::VulkanCore(): missing support for one or more validation layers, exiting.\n");
    }

    if (glfwCreateWindowSurface(m_instance, window, nullptr, &m_surface) != VK_SUCCESS) {
        LogErrorAndExit("VulkanCore::VulkanCore(): can't create window surface, exiting.\n");
    }

    m_physicalDevice = pickBestPhysicalDevice();

    findQueueFamilyIndices(m_physicalDevice, m_surface);
    m_device = createDevice(m_physicalDevice);

    vkGetDeviceQueue(m_device, m_presentQueue.familyIndex, 0, &m_presentQueue.queue);
    vkGetDeviceQueue(m_device, m_graphicsQueue.familyIndex, 0, &m_graphicsQueue.queue);
    vkGetDeviceQueue(m_device, m_computeQueue.familyIndex, 0, &m_computeQueue.queue);
}

VulkanCore::~VulkanCore()
{
    vkDestroyDevice(m_device, nullptr);
    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);

    if (m_messenger.has_value()) {
        auto destroyFunc = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT");
        ASSERT(destroyFunc != nullptr);
        destroyFunc(m_instance, m_messenger.value(), nullptr);
    }

    vkDestroyInstance(m_instance, nullptr);
}

VkSurfaceFormatKHR VulkanCore::pickBestSurfaceFormat() const
{
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, surfaceFormats.data());

    for (const auto& format : surfaceFormats) {
        // We use the *_UNORM format since "working directly with SRGB colors is a little bit challenging"
        // (https://vulkan-tutorial.com/Drawing_a_triangle/Presentation/Swap_chain). I don't really know what that's about..
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            LogInfo("VulkanCore::pickBestSurfaceFormat(): picked optimal RGBA8 sRGB surface format.\n");
            return format;
        }
    }

    // If we didn't find the optimal one, just chose an arbitrary one
    LogInfo("VulkanCore::pickBestSurfaceFormat(): couldn't find optimal surface format, so picked arbitrary supported format.\n");
    VkSurfaceFormatKHR format = surfaceFormats[0];

    if (format.colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        LogWarning("VulkanCore::pickBestSurfaceFormat(): could not find a sRGB surface format, so images won't be pretty!\n");
    }

    return format;
}

VkPresentModeKHR VulkanCore::pickBestPresentMode() const
{
    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, presentModes.data());

    for (const auto& mode : presentModes) {
        // Try to chose the mailbox mode, i.e. use-last-fully-generated-image mode
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            LogInfo("VulkanCore::pickBestPresentMode(): picked optimal mailbox present mode.\n");
            return mode;
        }
    }

    // VK_PRESENT_MODE_FIFO_KHR is guaranteed to be available and it basically corresponds to normal v-sync so it's fine
    LogInfo("VulkanCore::pickBestPresentMode(): picked standard FIFO present mode.\n");
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanCore::pickBestSwapchainExtent() const
{
    VkSurfaceCapabilitiesKHR surfaceCapabilities {};

    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &surfaceCapabilities) != VK_SUCCESS) {
        LogErrorAndExit("VulkanCore::VulkanCore(): could not get surface capabilities, exiting.\n");
    }

    if (surfaceCapabilities.currentExtent.width != UINT32_MAX) {
        // The surface has specified the extent (probably to whatever the window extent is) and we should choose that
        LogInfo("VulkanCore::pickBestSwapchainExtent(): using optimal window extents for swap chain.\n");
        return surfaceCapabilities.currentExtent;
    }

    // The drivers are flexible, so let's choose something good that is within the the legal extents
    VkExtent2D extent = {};

    int framebufferWidth, framebufferHeight;
    glfwGetFramebufferSize(m_window, &framebufferWidth, &framebufferHeight);

    extent.width = std::clamp(static_cast<uint32_t>(framebufferWidth), surfaceCapabilities.minImageExtent.width, surfaceCapabilities.maxImageExtent.width);
    extent.height = std::clamp(static_cast<uint32_t>(framebufferHeight), surfaceCapabilities.minImageExtent.height, surfaceCapabilities.maxImageExtent.height);
    LogInfo("VulkanCore::pickBestSwapchainExtent(): using specified extents (%u x %u) for swap chain.\n", extent.width, extent.height);

    return extent;
}

VulkanQueue VulkanCore::presentQueue() const
{
    return m_presentQueue;
}

VulkanQueue VulkanCore::graphicsQueue() const
{
    return m_graphicsQueue;
}

VkBool32 VulkanCore::debugMessageCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageTypes,
                                          const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
{
    LogError("VulkanCore::debugMessageCallback(): %s\n", pCallbackData->pMessage);
    return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT VulkanCore::debugMessengerCreateInfo() const
{
    VkDebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };

    debugMessengerCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT; // NOLINT(hicpp-signed-bitwise)
    debugMessengerCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT; // NOLINT(hicpp-signed-bitwise)
    debugMessengerCreateInfo.pfnUserCallback = debugMessageCallback;
    debugMessengerCreateInfo.pUserData = nullptr;

    return debugMessengerCreateInfo;
}

VkDebugUtilsMessengerEXT VulkanCore::createDebugMessenger(VkInstance instance, VkDebugUtilsMessengerCreateInfoEXT* createInfo) const
{
    auto createFunc = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (!createFunc) {
        LogErrorAndExit("VulkanCore::createDebugMessenger(): could not get function 'vkCreateDebugUtilsMessengerEXT', exiting.\n");
    }

    VkDebugUtilsMessengerEXT messenger;
    if (createFunc(instance, createInfo, nullptr, &messenger) != VK_SUCCESS) {
        LogErrorAndExit("VulkanCore::createDebugMessenger(): could not create the debug messenger, exiting.\n");
    }

    return messenger;
}

VkPhysicalDevice VulkanCore::pickBestPhysicalDevice() const
{
    uint32_t count;
    vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
    if (count < 1) {
        LogErrorAndExit("VulkanCore::pickBestPhysicalDevice(): could not find any physical devices with Vulkan support, exiting.\n");
    }

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(m_instance, &count, devices.data());

    if (count > 1) {
        LogWarning("VulkanCore::pickBestPhysicalDevice(): more than one physical device available, one will be chosen arbitrarily (FIXME!)\n");
    }

    // FIXME: Don't just pick the first one if there are more than one!
    VkPhysicalDevice physicalDevice = devices[0];

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physicalDevice, &props);
    LogInfo("VulkanCore::pickBestPhysicalDevice(): using physical device '%s'\n", props.deviceName);

    return physicalDevice;
}

VkInstance VulkanCore::createInstance(VkDebugUtilsMessengerCreateInfoEXT* debugMessengerCreateInfo) const
{
    VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    appInfo.pApplicationName = "ArkoseRenderer";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0); // NOLINT(hicpp-signed-bitwise)
    appInfo.pEngineName = "ArkoseRendererEngine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0); // NOLINT(hicpp-signed-bitwise)
    appInfo.apiVersion = VK_API_VERSION_1_1; // NOLINT(hicpp-signed-bitwise)

    // See https://www.lunarg.com/wp-content/uploads/2019/02/GPU-Assisted-Validation_v3_02_22_19.pdf for information
    VkValidationFeatureEnableEXT enables[] = { VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT };
    VkValidationFeaturesEXT validationFeatures { VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT };
    validationFeatures.enabledValidationFeatureCount = 1;
    validationFeatures.pEnabledValidationFeatures = enables;

    VkInstanceCreateInfo instanceCreateInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    instanceCreateInfo.pApplicationInfo = &appInfo;

    if (debugMessageCallback) {
        instanceCreateInfo.pNext = &validationFeatures;
        validationFeatures.pNext = debugMessengerCreateInfo;
    }

    const auto& extensions = instanceExtensions();
    instanceCreateInfo.enabledExtensionCount = extensions.size();
    instanceCreateInfo.ppEnabledExtensionNames = extensions.data();

    // NOTE: Support for the active validation layers should already be checked!
    instanceCreateInfo.enabledLayerCount = m_activeValidationLayers.size();
    instanceCreateInfo.ppEnabledLayerNames = m_activeValidationLayers.data();

    VkInstance instance;
    if (vkCreateInstance(&instanceCreateInfo, nullptr, &instance) != VK_SUCCESS) {
        LogErrorAndExit("VulkanCore::createInstance(): could not create instance.\n");
    }

    return instance;
}

VkDevice VulkanCore::createDevice(VkPhysicalDevice physicalDevice)
{
    // TODO: Allow users to specify beforehand that they e.g. might want 2 compute queues.
    std::unordered_set<uint32_t> queueFamilyIndices = { m_graphicsQueue.familyIndex, m_presentQueue.familyIndex };
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    const float queuePriority = 1.0f;
    for (uint32_t familyIndex : queueFamilyIndices) {

        VkDeviceQueueCreateInfo queueCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        queueCreateInfo.queueFamilyIndex = familyIndex;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfo.queueCount = 1;

        queueCreateInfos.push_back(queueCreateInfo);
    }

    //

    // TODO: How are we supposed to add and check support for these advanced features & extensions?

    VkPhysicalDeviceFeatures requestedDeviceFeatures = {};
    requestedDeviceFeatures.samplerAnisotropy = VK_TRUE;
    requestedDeviceFeatures.fillModeNonSolid = VK_TRUE;
    requestedDeviceFeatures.fragmentStoresAndAtomics = VK_TRUE;
    requestedDeviceFeatures.vertexPipelineStoresAndAtomics = VK_TRUE;
    requestedDeviceFeatures.shaderSampledImageArrayDynamicIndexing = VK_TRUE;
    requestedDeviceFeatures.shaderStorageBufferArrayDynamicIndexing = VK_TRUE;

    VkPhysicalDeviceDescriptorIndexingFeatures indexingFeatures { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES };
    indexingFeatures.descriptorBindingPartiallyBound = VK_TRUE;
    indexingFeatures.runtimeDescriptorArray = VK_TRUE;

    VkPhysicalDevice8BitStorageFeatures eightBitStorageFeatures { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES };
    eightBitStorageFeatures.storageBuffer8BitAccess = VK_TRUE; // (required if the extention is available)
    eightBitStorageFeatures.uniformAndStorageBuffer8BitAccess = VK_TRUE;
    eightBitStorageFeatures.storagePushConstant8 = VK_TRUE;

    VkPhysicalDevice16BitStorageFeatures sixteenBitStorageFeatures { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES };
    sixteenBitStorageFeatures.storageBuffer16BitAccess = VK_TRUE;
    sixteenBitStorageFeatures.uniformAndStorageBuffer16BitAccess = VK_TRUE;
    sixteenBitStorageFeatures.storagePushConstant16 = VK_TRUE;
    sixteenBitStorageFeatures.storageInputOutput16 = VK_FALSE;

    VkPhysicalDeviceShaderFloat16Int8Features shaderSmallTypeFeatures { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES };
    shaderSmallTypeFeatures.shaderFloat16 = VK_FALSE;
    shaderSmallTypeFeatures.shaderInt8 = VK_TRUE;

    std::vector<const char*> deviceExtensions {};
    deviceExtensions.emplace_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    deviceExtensions.emplace_back(VK_NV_RAY_TRACING_EXTENSION_NAME);
    deviceExtensions.emplace_back(VK_KHR_STORAGE_BUFFER_STORAGE_CLASS_EXTENSION_NAME);
    deviceExtensions.emplace_back(VK_KHR_8BIT_STORAGE_EXTENSION_NAME);
    deviceExtensions.emplace_back(VK_KHR_16BIT_STORAGE_EXTENSION_NAME);
    deviceExtensions.emplace_back(VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME);

    //

    VkDeviceCreateInfo deviceCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };

    deviceCreateInfo.queueCreateInfoCount = queueCreateInfos.size();
    deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();

    // (the support of these layers should already have been checked)
    deviceCreateInfo.enabledLayerCount = m_activeValidationLayers.size();
    deviceCreateInfo.ppEnabledLayerNames = m_activeValidationLayers.data();

    deviceCreateInfo.enabledExtensionCount = deviceExtensions.size();
    deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();

    deviceCreateInfo.pEnabledFeatures = &requestedDeviceFeatures;

    // Device features extention chain
    deviceCreateInfo.pNext = &indexingFeatures;
    indexingFeatures.pNext = &eightBitStorageFeatures;
    eightBitStorageFeatures.pNext = &sixteenBitStorageFeatures;
    sixteenBitStorageFeatures.pNext = &shaderSmallTypeFeatures;

    VkDevice device;
    if (vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device) != VK_SUCCESS) {
        LogErrorAndExit("VulkanCore::createDevice(): could not create a device, exiting.\n");
    }

    return device;
}

void VulkanCore::findQueueFamilyIndices(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface)
{
    uint32_t count;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, queueFamilies.data());

    bool foundGraphicsQueue = false;
    bool foundComputeQueue = false;
    bool foundPresentQueue = false;

    for (uint32_t idx = 0; idx < count; ++idx) {
        const auto& queueFamily = queueFamilies[idx];

        if (!foundGraphicsQueue && queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            m_graphicsQueue.familyIndex = idx;
            foundGraphicsQueue = true;
        }

        if (!foundComputeQueue && queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) {
            m_computeQueue.familyIndex = idx;
            foundComputeQueue = true;
        }

        if (!foundPresentQueue) {
            VkBool32 presentSupportForQueue;
            vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, idx, surface, &presentSupportForQueue);
            if (presentSupportForQueue) {
                m_presentQueue.familyIndex = idx;
                foundPresentQueue = true;
            }
        }
    }

    if (!foundGraphicsQueue) {
        LogErrorAndExit("VulkanCore::findQueueFamilyIndices(): could not find a graphics queue, exiting.\n");
    }
    if (!foundComputeQueue) {
        LogErrorAndExit("VulkanCore::findQueueFamilyIndices(): could not find a compute queue, exiting.\n");
    }
    if (!foundPresentQueue) {
        LogErrorAndExit("VulkanCore::findQueueFamilyIndices(): could not find a present queue, exiting.\n");
    }
}

bool VulkanCore::hasCombinedGraphicsComputeQueue() const
{
    return m_graphicsQueue.familyIndex == m_computeQueue.familyIndex;
}

std::vector<const char*> VulkanCore::instanceExtensions() const
{
    std::vector<const char*> extensions;

    // GLFW requires a few for basic presenting etc.
    uint32_t requiredCount;
    const char** requiredExtensions = glfwGetRequiredInstanceExtensions(&requiredCount);
    while (requiredCount--) {
        extensions.emplace_back(requiredExtensions[requiredCount]);
    }

    // For debug messages etc.
    extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    // For later spec (e.g. ray tracing stuff) queries
    extensions.emplace_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

    return extensions;
}

bool VulkanCore::verifyValidationLayerSupport() const
{
    uint32_t availableLayerCount;
    vkEnumerateInstanceLayerProperties(&availableLayerCount, nullptr);
    std::vector<VkLayerProperties> availableLayers(availableLayerCount);
    vkEnumerateInstanceLayerProperties(&availableLayerCount, availableLayers.data());

    bool fullSupport = true;
    for (const char* layer : m_activeValidationLayers) {
        bool found = false;
        for (auto availableLayer : availableLayers) {
            if (std::strcmp(layer, availableLayer.layerName) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            LogError("VulkanCore::checkValidationLayerSupport(): layer '%s' is not supported.\n", layer);
            fullSupport = false;
        }
    }

    return fullSupport;
}
