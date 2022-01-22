#include "VulkanBackend.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "VulkanCommandList.h"
#include "backend/vulkan/VulkanResources.h"
#include "backend/shader/Shader.h"
#include "backend/shader/ShaderManager.h"
#include "rendering/Registry.h"
#include "utility/FileIO.h"
#include "utility/Logging.h"
#include "utility/Profiling.h"
#include "utility/util.h"
#include <algorithm>
#include <cstring>
#include <fmt/format.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <spirv_cross/spirv_cross.hpp>
#include <unordered_map>
#include <unordered_set>

static bool s_unhandledWindowResize = false;

VulkanBackend::VulkanBackend(GLFWwindow* window, const AppSpecification& appSpecification)
    : m_window(window)
{
    glfwSetFramebufferSizeCallback(window, static_cast<GLFWframebuffersizefun>([](GLFWwindow* window, int width, int height) {
                                       // Is this even needed? Doesn't seem to be on Windows at least.
                                       s_unhandledWindowResize = true;
                                   }));

    {
        uint32_t availableLayerCount;
        vkEnumerateInstanceLayerProperties(&availableLayerCount, nullptr);
        std::vector<VkLayerProperties> availableLayers(availableLayerCount);
        vkEnumerateInstanceLayerProperties(&availableLayerCount, availableLayers.data());
        for (auto& layer : availableLayers)
            m_availableLayers.insert(layer.layerName);
    }

    {
        uint32_t availableInstanceExtensionsCount;
        vkEnumerateInstanceExtensionProperties(nullptr, &availableInstanceExtensionsCount, nullptr);
        std::vector<VkExtensionProperties> availableInstanceExtensions(availableInstanceExtensionsCount);
        vkEnumerateInstanceExtensionProperties(nullptr, &availableInstanceExtensionsCount, availableInstanceExtensions.data());
        for (auto& extension : availableInstanceExtensions)
            m_availableInstanceExtensions.insert(extension.extensionName);
    }

    std::vector<const char*> requestedLayers;

    if (vulkanDebugMode) {
        LogInfo("VulkanBackend: debug mode enabled!\n");

        ASSERT(hasSupportForLayer("VK_LAYER_KHRONOS_validation"));
        requestedLayers.emplace_back("VK_LAYER_KHRONOS_validation");

        auto dbgMessengerCreateInfo = VulkanDebugUtils::debugMessengerCreateInfo();
        m_instance = createInstance(requestedLayers, &dbgMessengerCreateInfo);

        m_debugUtils = std::make_unique<VulkanDebugUtils>(*this, m_instance);
        if (debugUtils().vkCreateDebugUtilsMessengerEXT(m_instance, &dbgMessengerCreateInfo, nullptr, &m_debugMessenger) != VK_SUCCESS) {
            LogErrorAndExit("VulkanBackend: could not create the debug messenger, exiting.\n");
        }

        VkDebugReportCallbackCreateInfoEXT dbgReportCreateInfo = { VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT };
        dbgReportCreateInfo.pfnCallback = VulkanDebugUtils::debugReportCallback;
        dbgReportCreateInfo.flags = VK_DEBUG_REPORT_INFORMATION_BIT_EXT;
        dbgReportCreateInfo.pUserData = nullptr;

        if (debugUtils().vkCreateDebugReportCallbackEXT(m_instance, &dbgReportCreateInfo, nullptr, &m_debugReportCallback) != VK_SUCCESS) {
            LogErrorAndExit("VulkanBackend: could not create the debug reporter, exiting.\n");
        }

    } else {
        m_instance = createInstance(requestedLayers, nullptr);
    }

    if (glfwCreateWindowSurface(m_instance, window, nullptr, &m_surface) != VK_SUCCESS)
        LogErrorAndExit("VulkanBackend: can't create window surface, exiting.\n");

    m_physicalDevice = pickBestPhysicalDevice();
    vkGetPhysicalDeviceProperties(physicalDevice(), &m_physicalDeviceProperties);
    LogInfo("VulkanBackend: using physical device '%s'\n", m_physicalDeviceProperties.deviceName);

    findQueueFamilyIndices(m_physicalDevice, m_surface);

    {
        uint32_t extensionCount;
        vkEnumerateDeviceExtensionProperties(physicalDevice(), nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> availableExtensions { extensionCount };
        vkEnumerateDeviceExtensionProperties(physicalDevice(), nullptr, &extensionCount, availableExtensions.data());
        for (auto& ext : availableExtensions)
            m_availableExtensions.insert(ext.extensionName);
    }

    if (!collectAndVerifyCapabilitySupport(appSpecification))
        LogErrorAndExit("VulkanBackend: could not verify support for all capabilities required by the app\n");

    m_device = createDevice(requestedLayers, m_physicalDevice);

    vkGetDeviceQueue(m_device, m_presentQueue.familyIndex, 0, &m_presentQueue.queue);
    vkGetDeviceQueue(m_device, m_graphicsQueue.familyIndex, 0, &m_graphicsQueue.queue);
    vkGetDeviceQueue(m_device, m_computeQueue.familyIndex, 0, &m_computeQueue.queue);

    if (hasActiveCapability(Backend::Capability::RtxRayTracing)) {
        m_rtx = std::make_unique<VulkanRTX>(*this, physicalDevice(), device());
    }

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.instance = m_instance;
    allocatorInfo.physicalDevice = physicalDevice();
    allocatorInfo.device = device();
    allocatorInfo.flags = 0u;
    if (vmaCreateAllocator(&allocatorInfo, &m_memoryAllocator) != VK_SUCCESS) {
        LogErrorAndExit("VulkanBackend: could not create memory allocator, exiting.\n");
    }

    VkCommandPoolCreateInfo poolCreateInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    poolCreateInfo.queueFamilyIndex = m_graphicsQueue.familyIndex;
    poolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; // (so we can easily reuse them each frame)
    if (vkCreateCommandPool(device(), &poolCreateInfo, nullptr, &m_defaultCommandPool) != VK_SUCCESS) {
        LogErrorAndExit("VulkanBackend: could not create command pool for the graphics queue, exiting.\n");
    }

    VkCommandPoolCreateInfo transientPoolCreateInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    transientPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    transientPoolCreateInfo.queueFamilyIndex = m_graphicsQueue.familyIndex;
    if (vkCreateCommandPool(device(), &transientPoolCreateInfo, nullptr, &m_transientCommandPool) != VK_SUCCESS) {
        LogErrorAndExit("VulkanBackend: could not create transient command pool, exiting.\n");
    }

    m_pipelineCache = createAndLoadPipelineCacheFromDisk();

    createSwapchain(physicalDevice(), device(), m_surface);
    createFrameContexts();

    setupDearImgui();

    m_persistentRegistry = std::make_unique<Registry>(*this, nullptr);
}

VulkanBackend::~VulkanBackend()
{
    // Before destroying stuff, make sure we're done with all scheduled work
    vkDeviceWaitIdle(device());

    m_pipelineRegistry.reset();
    m_persistentRegistry.reset();

    destroyDearImgui();

    destroyFrameRenderTargets();
    destroyFrameContexts();
    destroySwapchain();

    savePipelineCacheToDisk(m_pipelineCache);
    vkDestroyPipelineCache(device(), m_pipelineCache, nullptr);

    vkDestroyCommandPool(device(), m_defaultCommandPool, nullptr);
    vkDestroyCommandPool(device(), m_transientCommandPool, nullptr);

    vmaDestroyAllocator(m_memoryAllocator);

    vkDestroyDevice(m_device, nullptr);
    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);

    if (vulkanDebugMode) {
        debugUtils().vkDestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
        debugUtils().vkDestroyDebugReportCallbackEXT(m_instance, m_debugReportCallback, nullptr);
    }

    vkDestroyInstance(m_instance, nullptr);
}

bool VulkanBackend::hasActiveCapability(Capability capability) const
{
    auto it = m_activeCapabilities.find(capability);
    if (it == m_activeCapabilities.end())
        return false;
    return it->second;
}

bool VulkanBackend::hasSupportForLayer(const std::string& name) const
{
    auto it = m_availableLayers.find(name);
    if (it == m_availableLayers.end())
        return false;
    return true;
}

bool VulkanBackend::hasSupportForExtension(const std::string& name) const
{
    if (m_physicalDevice == VK_NULL_HANDLE)
        LogErrorAndExit("Checking support for extension but no physical device exist yet. Maybe you meant to check for instance extensions?\n");

    auto it = m_availableExtensions.find(name);
    if (it == m_availableExtensions.end())
        return false;
    return true;
}

bool VulkanBackend::hasSupportForInstanceExtension(const std::string& name) const
{
    auto it = m_availableInstanceExtensions.find(name);
    if (it == m_availableInstanceExtensions.end())
        return false;
    return true;
}

bool VulkanBackend::collectAndVerifyCapabilitySupport(const AppSpecification& appSpecification)
{
    VkPhysicalDeviceFeatures2 features2 { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    const VkPhysicalDeviceFeatures& features = features2.features;

    VkPhysicalDeviceVulkan11Features vk11features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
    features2.pNext = &vk11features;

    VkPhysicalDeviceVulkan12Features vk12features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    vk11features.pNext = &vk12features;

    vkGetPhysicalDeviceFeatures2(physicalDevice(), &features2);

    auto isSupported = [&](Capability capability) -> bool {
        switch (capability) {
        case Capability::RtxRayTracing:
            return hasSupportForExtension(VK_NV_RAY_TRACING_EXTENSION_NAME);
        case Capability::Shader16BitFloat:
            return vk11features.storageBuffer16BitAccess
                && vk11features.uniformAndStorageBuffer16BitAccess
                && vk11features.storageInputOutput16
                && vk11features.storagePushConstant16
                && vk12features.shaderFloat16;
        default:
            ASSERT_NOT_REACHED();
            return false;
        }
    };

    bool allRequiredSupported = true;

    if (!features.samplerAnisotropy || !features.fillModeNonSolid || !features.fragmentStoresAndAtomics || !features.vertexPipelineStoresAndAtomics) {
        LogError("VulkanBackend: no support for required common device feature\n");
        allRequiredSupported = false;
    }

    if (!features.shaderUniformBufferArrayDynamicIndexing || !vk12features.shaderUniformBufferArrayNonUniformIndexing ||
        !features.shaderStorageBufferArrayDynamicIndexing || !vk12features.shaderStorageBufferArrayNonUniformIndexing ||
        !features.shaderStorageImageArrayDynamicIndexing || !vk12features.shaderStorageImageArrayNonUniformIndexing ||
        !features.shaderSampledImageArrayDynamicIndexing || !vk12features.shaderSampledImageArrayNonUniformIndexing ||
        !vk12features.runtimeDescriptorArray || !vk12features.descriptorBindingVariableDescriptorCount) {
        LogError("VulkanBackend: no support for required common dynamic & non-uniform indexing device features\n");
        allRequiredSupported = false;
    }

    if (!vk12features.scalarBlockLayout) {
        LogError("VulkanBackend: no support for scalar layout in shader storage blocks\n");
        allRequiredSupported = false;
    }

    if (!vk12features.drawIndirectCount) {
        LogError("VulkanBackend: no support for required common drawing related device features\n");
        allRequiredSupported = false;
    }

    if (!vk12features.imagelessFramebuffer) {
        LogError("VulkanBackend: no support for imageless framebuffers which is required\n");
        allRequiredSupported = false;
    }

    if (vulkanDebugMode && !(vk12features.bufferDeviceAddress && vk12features.bufferDeviceAddressCaptureReplay)) {
        LogError("VulkanBackend: no support for buffer device address & buffer device address capture replay, which is required by e.g. Nsight for debugging. "
                 "If this is a problem, try compiling and running with vulkanDebugMode set to false.\n");
        allRequiredSupported = false;
    }

    for (auto& cap : appSpecification.requiredCapabilities) {
        if (isSupported(cap)) {
            m_activeCapabilities[cap] = true;
        } else {
            LogError("VulkanBackend: no support for required '%s' capability\n", capabilityName(cap).c_str());
            allRequiredSupported = false;
        }
    }

    for (auto& cap : appSpecification.optionalCapabilities) {
        if (isSupported(cap)) {
            m_activeCapabilities[cap] = true;
        } else {
            LogInfo("VulkanBackend: no support for optional '%s' capability\n", capabilityName(cap).c_str());
        }
    }

    return allRequiredSupported;
}

std::unique_ptr<Buffer> VulkanBackend::createBuffer(size_t size, Buffer::Usage usage, Buffer::MemoryHint memoryHint)
{
    return std::make_unique<VulkanBuffer>(*this, size, usage, memoryHint);
}

std::unique_ptr<RenderTarget> VulkanBackend::createRenderTarget(std::vector<RenderTarget::Attachment> attachments)
{
    bool imageless = false; // for now, keep using normal framebuffers for these generic render targets
    return std::make_unique<VulkanRenderTarget>(*this, attachments, imageless, VulkanRenderTarget::QuirkMode::None);
}

std::unique_ptr<Texture> VulkanBackend::createTexture(Texture::TextureDescription desc)
{
    return std::make_unique<VulkanTexture>(*this, desc);
}

std::unique_ptr<BindingSet> VulkanBackend::createBindingSet(std::vector<ShaderBinding> shaderBindings)
{
    return std::make_unique<VulkanBindingSet>(*this, shaderBindings);
}

std::unique_ptr<RenderState> VulkanBackend::createRenderState(const RenderTarget& renderTarget, const VertexLayout& vertexLayout,
                                                              const Shader& shader, const StateBindings& stateBindings,
                                                              const Viewport& viewport, const BlendState& blendState, const RasterState& rasterState, const DepthState& depthState, const StencilState& stencilState)
{
    return std::make_unique<VulkanRenderState>(*this, renderTarget, vertexLayout, shader, stateBindings, viewport, blendState, rasterState, depthState, stencilState);
}

std::unique_ptr<BottomLevelAS> VulkanBackend::createBottomLevelAccelerationStructure(std::vector<RTGeometry> geometries)
{
    return std::make_unique<VulkanBottomLevelAS>(*this, geometries);
}

std::unique_ptr<TopLevelAS> VulkanBackend::createTopLevelAccelerationStructure(std::vector<RTGeometryInstance> instances)
{
    return std::make_unique<VulkanTopLevelAS>(*this, instances);
}

std::unique_ptr<RayTracingState> VulkanBackend::createRayTracingState(ShaderBindingTable& sbt, const StateBindings& stateBindings, uint32_t maxRecursionDepth)
{
    return std::make_unique<VulkanRayTracingState>(*this, sbt, stateBindings, maxRecursionDepth);
}

std::unique_ptr<ComputeState> VulkanBackend::createComputeState(const Shader& shader, std::vector<BindingSet*> bidningSets)
{
    return std::make_unique<VulkanComputeState>(*this, shader, bidningSets);
}

VkSurfaceFormatKHR VulkanBackend::pickBestSurfaceFormat() const
{
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, surfaceFormats.data());

    for (const auto& format : surfaceFormats) {
        // We use the *_UNORM format since "working directly with SRGB colors is a little bit challenging"
        // (https://vulkan-tutorial.com/Drawing_a_triangle/Presentation/Swap_chain). I don't really know what that's about..
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            LogInfo("VulkanBackend: picked optimal RGBA8 sRGB surface format.\n");
            return format;
        }
    }

    // If we didn't find the optimal one, just chose an arbitrary one
    LogInfo("VulkanBackend: couldn't find optimal surface format, so picked arbitrary supported format.\n");
    VkSurfaceFormatKHR format = surfaceFormats[0];

    if (format.colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        LogWarning("VulkanBackend: could not find a sRGB surface format, so images won't be pretty!\n");
    }

    return format;
}

VkPresentModeKHR VulkanBackend::pickBestPresentMode() const
{
    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, presentModes.data());

    for (const auto& mode : presentModes) {
        // Try to chose the mailbox mode, i.e. use-last-fully-generated-image mode
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            LogInfo("VulkanBackend: picked optimal mailbox present mode.\n");
            return mode;
        }
    }

    // VK_PRESENT_MODE_FIFO_KHR is guaranteed to be available and it basically corresponds to normal v-sync so it's fine
    LogInfo("VulkanBackend: picked standard FIFO present mode.\n");
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanBackend::pickBestSwapchainExtent() const
{
    VkSurfaceCapabilitiesKHR surfaceCapabilities {};

    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &surfaceCapabilities) != VK_SUCCESS) {
        LogErrorAndExit("VulkanBackend: could not get surface capabilities, exiting.\n");
    }

    if (surfaceCapabilities.currentExtent.width != UINT32_MAX) {
        // The surface has specified the extent (probably to whatever the window extent is) and we should choose that
        LogInfo("VulkanBackend: using optimal window extents for swap chain.\n");
        return surfaceCapabilities.currentExtent;
    }

    // The drivers are flexible, so let's choose something good that is within the the legal extents
    VkExtent2D extent = {};

    int framebufferWidth, framebufferHeight;
    glfwGetFramebufferSize(m_window, &framebufferWidth, &framebufferHeight);

    extent.width = std::clamp(static_cast<uint32_t>(framebufferWidth), surfaceCapabilities.minImageExtent.width, surfaceCapabilities.maxImageExtent.width);
    extent.height = std::clamp(static_cast<uint32_t>(framebufferHeight), surfaceCapabilities.minImageExtent.height, surfaceCapabilities.maxImageExtent.height);
    LogInfo("VulkanBackend: using specified extents (%u x %u) for swap chain.\n", extent.width, extent.height);

    return extent;
}

VkInstance VulkanBackend::createInstance(const std::vector<const char*>& requestedLayers, VkDebugUtilsMessengerCreateInfoEXT* debugMessengerCreateInfo) const
{
    SCOPED_PROFILE_ZONE_BACKEND();

    for (auto& layer : requestedLayers) {
        if (!hasSupportForLayer(layer))
            LogErrorAndExit("VulkanBackend: missing layer '%s'\n", layer);
    }

    bool includeValidationFeatures = false;
    std::vector<const char*> instanceExtensions;
    {
        uint32_t requiredCount;
        const char** requiredExtensions = glfwGetRequiredInstanceExtensions(&requiredCount);
        for (uint32_t i = 0; i < requiredCount; ++i) {
            const char* name = requiredExtensions[i];
            ASSERT(hasSupportForInstanceExtension(name));
            instanceExtensions.emplace_back(name);
        }

        // Required for checking support of complex features. It's probably fine to always require it. If it doesn't exist, we deal with it then..
        ASSERT(hasSupportForInstanceExtension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME));
        instanceExtensions.emplace_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

        // For debug messages etc.
        if (vulkanDebugMode) {
            ASSERT(hasSupportForInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME));
            instanceExtensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

            if (hasSupportForInstanceExtension(VK_EXT_DEBUG_REPORT_EXTENSION_NAME)) {
                instanceExtensions.emplace_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
            }

            if (hasSupportForInstanceExtension(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME)) {
                instanceExtensions.emplace_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
                includeValidationFeatures = true;
            }
        }
    }

    VkValidationFeaturesEXT validationFeatures { VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT };
    std::vector<VkValidationFeatureEnableEXT> enabledValidationFeatures;
    {
        // See https://www.lunarg.com/wp-content/uploads/2019/02/GPU-Assisted-Validation_v3_02_22_19.pdf for information
        enabledValidationFeatures.push_back(VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT);
        enabledValidationFeatures.push_back(VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT);
        enabledValidationFeatures.push_back(VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT);
        enabledValidationFeatures.push_back(VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT);
        validationFeatures.enabledValidationFeatureCount = (uint32_t)enabledValidationFeatures.size();
        validationFeatures.pEnabledValidationFeatures = enabledValidationFeatures.data();
    }

    VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    appInfo.pApplicationName = "ArkoseRenderer";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0); // NOLINT(hicpp-signed-bitwise)
    appInfo.pEngineName = "ArkoseRendererEngine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0); // NOLINT(hicpp-signed-bitwise)
    appInfo.apiVersion = VK_API_VERSION_1_2; // NOLINT(hicpp-signed-bitwise)

    VkInstanceCreateInfo instanceCreateInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    instanceCreateInfo.pApplicationInfo = &appInfo;

    instanceCreateInfo.enabledExtensionCount = (uint32_t)instanceExtensions.size();
    instanceCreateInfo.ppEnabledExtensionNames = instanceExtensions.data();

    instanceCreateInfo.enabledLayerCount = (uint32_t)requestedLayers.size();
    instanceCreateInfo.ppEnabledLayerNames = requestedLayers.data();

    if (debugMessengerCreateInfo) {
        instanceCreateInfo.pNext = debugMessengerCreateInfo;
        if (includeValidationFeatures) {
            debugMessengerCreateInfo->pNext = &validationFeatures;
        }
    }

    VkInstance instance;
    if (vkCreateInstance(&instanceCreateInfo, nullptr, &instance) != VK_SUCCESS)
        LogErrorAndExit("VulkanBackend: could not create instance.\n");

    return instance;
}

VkDevice VulkanBackend::createDevice(const std::vector<const char*>& requestedLayers, VkPhysicalDevice physicalDevice)
{
    SCOPED_PROFILE_ZONE_BACKEND();

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

    std::vector<const char*> deviceExtensions {};

    ASSERT(hasSupportForExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME));
    deviceExtensions.emplace_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    if (vulkanDebugMode && hasSupportForExtension(VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME))
        deviceExtensions.emplace_back(VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME);

    VkPhysicalDeviceFeatures2 features2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    VkPhysicalDeviceFeatures& features = features2.features;
    VkPhysicalDeviceVulkan11Features vk11features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
    VkPhysicalDeviceVulkan12Features vk12features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };

    // Enable some very basic common features expected by everyone to exist
    features.samplerAnisotropy = VK_TRUE;
    features.fillModeNonSolid = VK_TRUE;
    features.fragmentStoresAndAtomics = VK_TRUE;
    features.vertexPipelineStoresAndAtomics = VK_TRUE;
    
    // Common dynamic & non-uniform indexing features that should be supported on a modern GPU
    features.shaderUniformBufferArrayDynamicIndexing = VK_TRUE;
    vk12features.shaderUniformBufferArrayNonUniformIndexing = VK_TRUE;
    features.shaderStorageBufferArrayDynamicIndexing = VK_TRUE;
    vk12features.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
    features.shaderStorageImageArrayDynamicIndexing = VK_TRUE;
    vk12features.shaderStorageImageArrayNonUniformIndexing = VK_TRUE;
    features.shaderSampledImageArrayDynamicIndexing = VK_TRUE;
    vk12features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    vk12features.runtimeDescriptorArray = VK_TRUE;
    vk12features.descriptorBindingVariableDescriptorCount = VK_TRUE;

    // Common drawing related features
    vk12features.drawIndirectCount = VK_TRUE;

    // Scalar block layout in shaders
    vk12features.scalarBlockLayout = VK_TRUE;

    // Imageless framebuffers
    vk12features.imagelessFramebuffer = VK_TRUE;

    // GPU debugging & insight for e.g. Nsight
    if (vulkanDebugMode) {
        vk12features.bufferDeviceAddress = VK_TRUE;
        vk12features.bufferDeviceAddressCaptureReplay = VK_TRUE;
    }

    for (auto& [capability, active] : m_activeCapabilities) {
        if (!active)
            continue;
        switch (capability) {
        case Capability::RtxRayTracing:
            deviceExtensions.push_back(VK_NV_RAY_TRACING_EXTENSION_NAME);
            break;
        case Capability::Shader16BitFloat:
            vk11features.storageBuffer16BitAccess = VK_TRUE;
            vk11features.uniformAndStorageBuffer16BitAccess = VK_TRUE;
            vk11features.storageInputOutput16 = VK_TRUE;
            vk11features.storagePushConstant16 = VK_TRUE;
            vk12features.shaderFloat16 = VK_TRUE;
            break;
        default:
            ASSERT_NOT_REACHED();
        }
    }

    VkDeviceCreateInfo deviceCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };

    deviceCreateInfo.queueCreateInfoCount = (uint32_t)queueCreateInfos.size();
    deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();

    // (the support of these requestedLayers should already have been checked)
    deviceCreateInfo.enabledLayerCount = (uint32_t)requestedLayers.size();
    deviceCreateInfo.ppEnabledLayerNames = requestedLayers.data();

    deviceCreateInfo.enabledExtensionCount = (uint32_t)deviceExtensions.size();
    deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();

    // Since we use VkPhysicalDeviceFeatures2 this should be null according to spec
    deviceCreateInfo.pEnabledFeatures = nullptr;

    // Device features extension chain
    deviceCreateInfo.pNext = &features2;
    features2.pNext = &vk11features;
    vk11features.pNext = &vk12features;

    VkDevice device;
    if (vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device) != VK_SUCCESS)
        LogErrorAndExit("VulkanBackend: could not create a device, exiting.\n");

    return device;
}

void VulkanBackend::findQueueFamilyIndices(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface)
{
    SCOPED_PROFILE_ZONE_BACKEND();

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
        LogErrorAndExit("VulkanBackend: could not find a graphics queue, exiting.\n");
    }
    if (!foundComputeQueue) {
        LogErrorAndExit("VulkanBackend: could not find a compute queue, exiting.\n");
    }
    if (!foundPresentQueue) {
        LogErrorAndExit("VulkanBackend: could not find a present queue, exiting.\n");
    }
}

VkPhysicalDevice VulkanBackend::pickBestPhysicalDevice() const
{
    SCOPED_PROFILE_ZONE_BACKEND();

    uint32_t count;
    vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
    if (count < 1) {
        LogErrorAndExit("VulkanBackend: could not find any physical devices with Vulkan support, exiting.\n");
    }

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(m_instance, &count, devices.data());

    if (count > 1) {
        LogWarning("VulkanBackend: more than one physical device available, one will be chosen arbitrarily (FIXME!)\n");
    }

    // FIXME: Don't just pick the first one if there are more than one!
    VkPhysicalDevice physicalDevice = devices[0];

    return physicalDevice;
}

VkPipelineCache VulkanBackend::createAndLoadPipelineCacheFromDisk() const
{
    SCOPED_PROFILE_ZONE_BACKEND();

    VkPipelineCacheCreateInfo pipelineCacheInfo = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };

    // TODO: Maybe do some validation on the data e.g. in case version change? On the other hand, it's easy to just delete the cache if it doesn't load properly..
    auto maybeCacheData = FileIO::readBinaryDataFromFile<char>(piplineCacheFilePath);
    if (maybeCacheData.has_value()) {
        const std::vector<char>& cacheData = maybeCacheData.value();
        pipelineCacheInfo.pInitialData = cacheData.data();
        pipelineCacheInfo.initialDataSize = cacheData.size();
    } else {
        pipelineCacheInfo.pInitialData = nullptr;
        pipelineCacheInfo.initialDataSize = 0;
    }

    VkPipelineCache pipelineCache;
    if (vkCreatePipelineCache(device(), &pipelineCacheInfo, nullptr, &pipelineCache) != VK_SUCCESS) {
        LogErrorAndExit("VulkanBackend: could not create pipeline cache, exiting.\n");
    }

    return pipelineCache;
}

void VulkanBackend::savePipelineCacheToDisk(VkPipelineCache pipelineCache) const
{
    SCOPED_PROFILE_ZONE_BACKEND();

    size_t size;
    vkGetPipelineCacheData(device(), pipelineCache, &size, nullptr);
    std::vector<char> data(size);
    vkGetPipelineCacheData(device(), pipelineCache, &size, data.data());

    FileIO::writeBinaryDataToFile(piplineCacheFilePath, data);
}

void VulkanBackend::createSwapchain(VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface)
{
    SCOPED_PROFILE_ZONE_BACKEND();

    VkSurfaceCapabilitiesKHR surfaceCapabilities;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCapabilities) != VK_SUCCESS) {
        LogErrorAndExit("VulkanBackend: could not get surface capabilities, exiting.\n");
    }

    VkSwapchainCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    createInfo.surface = surface;

    // See https://github.com/KhronosGroup/Vulkan-Docs/issues/909 for discussion regarding +1
    createInfo.minImageCount = surfaceCapabilities.minImageCount + 1;
    if (surfaceCapabilities.maxImageCount != 0) {
        createInfo.minImageCount = std::min(createInfo.minImageCount, surfaceCapabilities.maxImageCount);
    }

    VkSurfaceFormatKHR surfaceFormat = pickBestSurfaceFormat();
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    m_swapchainImageFormat = surfaceFormat.format;

    VkPresentModeKHR presentMode = pickBestPresentMode();
    createInfo.presentMode = presentMode;

    VkExtent2D swapchainExtent = pickBestSwapchainExtent();
    m_swapchainExtent = { swapchainExtent.width, swapchainExtent.height };
    createInfo.imageExtent = swapchainExtent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT; // TODO: What do we want here? Maybe this suffices?
    // TODO: Assure VK_IMAGE_USAGE_STORAGE_BIT is supported using vkGetPhysicalDeviceSurfaceCapabilitiesKHR & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT

    if (vulkanDebugMode) {
        // (for nsight debugging & similar stuff)
        createInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }

    uint32_t queueFamilyIndices[] = { m_graphicsQueue.familyIndex, m_presentQueue.familyIndex };
    if (m_graphicsQueue.familyIndex != m_computeQueue.familyIndex) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
        createInfo.queueFamilyIndexCount = 2;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = surfaceCapabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; // opaque swapchain
    createInfo.clipped = VK_TRUE; // clip pixels obscured by other windows etc.

    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &m_swapchain) != VK_SUCCESS) {
        LogErrorAndExit("VulkanBackend: could not create swapchain, exiting.\n");
    }

    uint32_t numSwapchainImages;
    vkGetSwapchainImagesKHR(device, m_swapchain, &numSwapchainImages, nullptr);
    std::vector<VkImage> swapchainImages { numSwapchainImages };
    vkGetSwapchainImagesKHR(device, m_swapchain, &numSwapchainImages, swapchainImages.data());

    for (uint32_t imageIdx = 0; imageIdx < numSwapchainImages; ++imageIdx) {

        auto swapchainImageContext = std::make_unique<SwapchainImageContext>();
        swapchainImageContext->image = swapchainImages[imageIdx];

        // Create image view
        {
            VkImageViewCreateInfo imageViewCreateInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };

            imageViewCreateInfo.image = swapchainImageContext->image;
            imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            imageViewCreateInfo.format = surfaceFormat.format;

            imageViewCreateInfo.components = {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY
            };

            imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
            imageViewCreateInfo.subresourceRange.levelCount = 1;

            imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
            imageViewCreateInfo.subresourceRange.layerCount = 1;

            imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

            VkImageView swapchainImageView;
            if (vkCreateImageView(device, &imageViewCreateInfo, nullptr, &swapchainImageView) != VK_SUCCESS) {
                LogErrorAndExit("VulkanBackend: could not create image view %u (out of %u), exiting.\n", imageIdx, numSwapchainImages);
            }

            swapchainImageContext->imageView = swapchainImageView;
        }

        // Create mock VulkanTexture for the swapchain image & its image view
        {
            auto mockTexture = std::make_unique<VulkanTexture>();

            mockTexture->m_type = Texture::Type::Texture2D;
            mockTexture->m_extent = m_swapchainExtent;
            mockTexture->m_format = Texture::Format::Unknown;
            mockTexture->m_minFilter = Texture::MinFilter::Nearest;
            mockTexture->m_magFilter = Texture::MagFilter::Nearest;
            mockTexture->m_wrapMode = { Texture::WrapMode::Repeat,
                                        Texture::WrapMode::Repeat,
                                        Texture::WrapMode::Repeat };
            mockTexture->m_mipmap = Texture::Mipmap::None;
            mockTexture->m_multisampling = Texture::Multisampling::None;

            mockTexture->vkUsage = createInfo.imageUsage;
            mockTexture->vkFormat = m_swapchainImageFormat;
            mockTexture->image = swapchainImageContext->image;
            mockTexture->imageView = swapchainImageContext->imageView;
            mockTexture->currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;

            swapchainImageContext->mockColorTexture = std::move(mockTexture);
        }

        // Create depth texture
        {
            Texture::TextureDescription depthDesc { .type = Texture::Type::Texture2D,
                                                    .arrayCount = 1u,
                                                    .extent = m_swapchainExtent,
                                                    .format = Texture::Format::Depth32F,
                                                    .minFilter = Texture::MinFilter::Nearest,
                                                    .magFilter = Texture::MagFilter::Nearest,
                                                    .wrapMode = {
                                                        Texture::WrapMode::Repeat,
                                                        Texture::WrapMode::Repeat,
                                                        Texture::WrapMode::Repeat },
                                                    .mipmap = Texture::Mipmap::None,
                                                    .multisampling = Texture::Multisampling::None };
            swapchainImageContext->depthTexture = std::make_unique<VulkanTexture>(*this, depthDesc);
        }

        m_swapchainImageContexts.push_back(std::move(swapchainImageContext));
    }

    if (m_guiIsSetup) {
        ImGui_ImplVulkan_SetMinImageCount(numSwapchainImages);
    }
}

void VulkanBackend::destroySwapchain()
{
    SCOPED_PROFILE_ZONE_BACKEND();

    for (auto& swapchainImageContext : m_swapchainImageContexts) {
        vkDestroyImageView(device(), swapchainImageContext->imageView, nullptr);
    }

    m_swapchainImageContexts.clear();

    vkDestroySwapchainKHR(device(), m_swapchain, nullptr);
}

Extent2D VulkanBackend::recreateSwapchain()
{
    SCOPED_PROFILE_ZONE_BACKEND();

    while (true) {
        // As long as we are minimized, don't do anything
        int windowFramebufferWidth, windowFramebufferHeight;
        glfwGetFramebufferSize(m_window, &windowFramebufferWidth, &windowFramebufferHeight);
        if (windowFramebufferWidth == 0 || windowFramebufferHeight == 0) {
            LogInfo("VulkanBackend: rendering paused since there are no pixels to draw to.\n");
            glfwWaitEvents();
        } else {
            LogInfo("VulkanBackend: rendering resumed.\n");
            break;
        }
    }

    vkDeviceWaitIdle(device());

    destroySwapchain();
    createSwapchain(physicalDevice(), device(), m_surface);

    SwapchainImageContext& referenceImageContext = *m_swapchainImageContexts[0];
    createFrameRenderTargets(referenceImageContext);

    m_relativeFrameIndex = 0;
    s_unhandledWindowResize = false;

    return m_swapchainExtent;
}

void VulkanBackend::createFrameContexts()
{
    // We need the swapchain to be created for reference!
    ASSERT(m_swapchainImageContexts.size() > 0);
    SwapchainImageContext& referenceImageContext = *m_swapchainImageContexts[0];

    for (int i = 0; i < NumInFlightFrames; ++i) {

        if (m_frameContexts[i] == nullptr)
            m_frameContexts[i] = std::make_unique<FrameContext>();
        FrameContext& frameContext = *m_frameContexts[i];

        // Create upload buffer
        {
            static constexpr size_t registryUploadBufferSize = 4 * 1024 * 1024;
            frameContext.uploadBuffer = std::make_unique<UploadBuffer>(*this, registryUploadBufferSize);
        }

        // Create fence
        {
            VkFenceCreateInfo fenceCreateInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
            fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

            if (vkCreateFence(device(), &fenceCreateInfo, nullptr, &frameContext.frameFence) != VK_SUCCESS) {
                LogErrorAndExit("VulkanBackend: could not create frame context fence, exiting.\n");
            }
        }

        // Create semaphores
        {
            VkSemaphoreCreateInfo semaphoreCreateInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

            if (vkCreateSemaphore(device(), &semaphoreCreateInfo, nullptr, &frameContext.imageAvailableSemaphore) != VK_SUCCESS) {
                LogErrorAndExit("VulkanBackend: could not create imageAvailableSemaphore, exiting.\n");
            }

            if (vkCreateSemaphore(device(), &semaphoreCreateInfo, nullptr, &frameContext.renderingFinishedSemaphore) != VK_SUCCESS) {
                LogErrorAndExit("VulkanBackend: could not create renderingFinishedSemaphore, exiting.\n");
            }
        }

        // Create command buffer for recoding this frame
        {
            VkCommandBufferAllocateInfo commandBufferAllocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
            commandBufferAllocateInfo.commandPool = m_defaultCommandPool;
            commandBufferAllocateInfo.commandBufferCount = 1;

            // Can be submitted to a queue for execution, but cannot be called from other command buffers
            commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

            VkCommandBuffer commandBuffer;
            if (vkAllocateCommandBuffers(device(), &commandBufferAllocateInfo, &commandBuffer) != VK_SUCCESS) {
                LogErrorAndExit("VulkanBackend: could not create command buffer, exiting.\n");
            }

            frameContext.commandBuffer = commandBuffer;
        }

        // Create timestamp query pool for this frame
        {
            VkQueryPoolCreateInfo timestampQueryPoolCreateInfo = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
            timestampQueryPoolCreateInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
            timestampQueryPoolCreateInfo.queryCount = FrameContext::TimestampQueryPoolCount;

            VkQueryPool timestampQueryPool;
            if (vkCreateQueryPool(device(), &timestampQueryPoolCreateInfo, nullptr, &timestampQueryPool) != VK_SUCCESS) {
                LogErrorAndExit("VulkanBackend: could not create timestamp query pool, exiting.\n");
            }

            frameContext.timestampQueryPool = timestampQueryPool;
        }

        createFrameRenderTargets(referenceImageContext);
    }
}

void VulkanBackend::destroyFrameContexts()
{
    for (std::unique_ptr<FrameContext>& frameContext : m_frameContexts) {
        vkDestroyQueryPool(device(), frameContext->timestampQueryPool, nullptr);
        vkFreeCommandBuffers(device(), m_defaultCommandPool, 1, &frameContext->commandBuffer);
        vkDestroySemaphore(device(), frameContext->imageAvailableSemaphore, nullptr);
        vkDestroySemaphore(device(), frameContext->renderingFinishedSemaphore, nullptr);
        vkDestroyFence(device(), frameContext->frameFence, nullptr);
        frameContext.reset();
    }
}

void VulkanBackend::createFrameRenderTargets(const SwapchainImageContext& referenceImageContext)
{
    // We use imageless framebuffers for these swapchain render targets!
    constexpr bool imageless = true;

    auto attachments = std::vector<RenderTarget::Attachment>({ { RenderTarget::AttachmentType::Color0, referenceImageContext.mockColorTexture.get(), LoadOp::Clear, StoreOp::Store },
                                                                { RenderTarget::AttachmentType::Depth, referenceImageContext.depthTexture.get(), LoadOp::Clear, StoreOp::Store } });
    m_clearingRenderTarget = std::make_unique<VulkanRenderTarget>(*this, attachments, imageless);

    // NOTE: Does not handle depth & requires something to have already been written to the render target, as it has load op load on color0
    auto finalAttachments = std::vector<RenderTarget::Attachment>({ { RenderTarget::AttachmentType::Color0, referenceImageContext.mockColorTexture.get(), LoadOp::Load, StoreOp::Store } });
    m_guiRenderTargetForPresenting = std::make_unique<VulkanRenderTarget>(*this, finalAttachments, imageless, VulkanRenderTarget::QuirkMode::ForPresenting);
}

void VulkanBackend::destroyFrameRenderTargets()
{
    m_clearingRenderTarget.reset();
    m_guiRenderTargetForPresenting.reset();
}

void VulkanBackend::setupDearImgui()
{
    SCOPED_PROFILE_ZONE_BACKEND();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForVulkan(m_window, true);

    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };
    VkDescriptorPoolCreateInfo descPoolCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    descPoolCreateInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    descPoolCreateInfo.maxSets = 1000 * IM_ARRAYSIZE(poolSizes);
    descPoolCreateInfo.poolSizeCount = (uint32_t)IM_ARRAYSIZE(poolSizes);
    descPoolCreateInfo.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(device(), &descPoolCreateInfo, nullptr, &m_guiDescriptorPool) != VK_SUCCESS) {
        LogErrorAndExit("DearImGui error while setting up descriptor pool\n");
    }

    ImGui_ImplVulkan_InitInfo initInfo = {};
    initInfo.CheckVkResultFn = [](VkResult result) {
        if (result != VK_SUCCESS) {
            LogErrorAndExit("DearImGui vulkan error!\n");
        }
    };

    initInfo.Instance = m_instance;
    initInfo.PhysicalDevice = physicalDevice();
    initInfo.Device = device();
    initInfo.Allocator = nullptr;

    initInfo.QueueFamily = m_graphicsQueue.familyIndex;
    initInfo.Queue = m_graphicsQueue.queue;

    initInfo.MinImageCount = (uint32_t)m_swapchainImageContexts.size(); // (todo: should this be something different than the actual count??)
    initInfo.ImageCount = (uint32_t)m_swapchainImageContexts.size();

    initInfo.DescriptorPool = m_guiDescriptorPool;
    initInfo.PipelineCache = VK_NULL_HANDLE;

    ASSERT(m_guiRenderTargetForPresenting != nullptr); // make sure this is created after the swapchain is created so we know what to render to!
    VkRenderPass compatibleRenderPassForImGui = m_guiRenderTargetForPresenting->compatibleRenderPass;
    ImGui_ImplVulkan_Init(&initInfo, compatibleRenderPassForImGui);

    issueSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
        ImGui_ImplVulkan_CreateFontsTexture(commandBuffer);
    });
    ImGui_ImplVulkan_DestroyFontUploadObjects();

    m_guiIsSetup = true;
}

void VulkanBackend::destroyDearImgui()
{
    vkDestroyDescriptorPool(device(), m_guiDescriptorPool, nullptr);

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    m_guiIsSetup = false;
}

void VulkanBackend::renderDearImguiFrame(VkCommandBuffer commandBuffer, FrameContext& frameContext, SwapchainImageContext& swapchainImageContext)
{
    VkRenderPassBeginInfo passBeginInfo = {};
    passBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    passBeginInfo.renderPass = m_guiRenderTargetForPresenting->compatibleRenderPass;
    passBeginInfo.framebuffer = m_guiRenderTargetForPresenting->framebuffer;
    passBeginInfo.renderArea.extent.width = m_swapchainExtent.width();
    passBeginInfo.renderArea.extent.height = m_swapchainExtent.height();
    passBeginInfo.clearValueCount = 0;
    passBeginInfo.pClearValues = nullptr;

    // NOTE: We use imageless framebuffer for swapchain images!
    VkRenderPassAttachmentBeginInfo attachmentBeginInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO };
    attachmentBeginInfo.pAttachments = &swapchainImageContext.imageView;
    attachmentBeginInfo.attachmentCount = 1;
    passBeginInfo.pNext = &attachmentBeginInfo;

    vkCmdBeginRenderPass(commandBuffer, &passBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
    vkCmdEndRenderPass(commandBuffer);

    VulkanTexture& swapchainTexture = *swapchainImageContext.mockColorTexture;
    swapchainTexture.currentLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
}

void VulkanBackend::newFrame(Scene& scene)
{
    SCOPED_PROFILE_ZONE_BACKEND();

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    int width, height;
    glfwGetFramebufferSize(m_window, &width, &height);
    scene.setMainViewportSize(badge(), { width, height });
}

bool VulkanBackend::executeFrame(const Scene& scene, RenderPipeline& renderPipeline, double elapsedTime, double deltaTime)
{
    SCOPED_PROFILE_ZONE_BACKEND();

    double cpuFrameStartTime = glfwGetTime();

    bool isRelativeFirstFrame = m_relativeFrameIndex < m_frameContexts.size();
    AppState appState { m_swapchainExtent, deltaTime, elapsedTime, m_currentFrameIndex, isRelativeFirstFrame };

    uint32_t frameContextIndex = m_currentFrameIndex % m_frameContexts.size();
    FrameContext& frameContext = *m_frameContexts[frameContextIndex];

    {
        SCOPED_PROFILE_ZONE_BACKEND_NAMED("Waiting for fence");
        if (vkWaitForFences(device(), 1, &frameContext.frameFence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
            LogError("VulkanBackend: error while waiting for in-flight frame fence (frame %u).\n", m_currentFrameIndex);
        }
    }

    uint32_t swapchainImageIndex;
    VkResult acquireResult;
    {
        {
            SCOPED_PROFILE_ZONE_BACKEND_NAMED("Acquiring next swapchain image");
            acquireResult = vkAcquireNextImageKHR(device(), m_swapchain, UINT64_MAX, frameContext.imageAvailableSemaphore, VK_NULL_HANDLE, &swapchainImageIndex);
        }

        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
            // Since we couldn't acquire an image to draw to, recreate the swapchain and report that it didn't work
            Extent2D newWindowExtent = recreateSwapchain();
            appState = appState.updateWindowExtent(newWindowExtent);
            reconstructRenderPipelineResources(renderPipeline);
            return false;
        }
        if (acquireResult == VK_SUBOPTIMAL_KHR) {
            // Since we did manage to acquire an image, just roll with it for now, but it will probably resolve itself after presenting
            LogWarning("VulkanBackend: next image was acquired but it's suboptimal, ignoring.\n");
        }

        if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
            LogError("VulkanBackend: error acquiring next swapchain image.\n");
        }
    }

    SwapchainImageContext& swapchainImageContext = *m_swapchainImageContexts[swapchainImageIndex].get();

    // We've just found out what image views we should use for this frame, so send them to the render target so it knows to bind them
    m_clearingRenderTarget->imagelessFramebufferAttachments = { swapchainImageContext.mockColorTexture->imageView, swapchainImageContext.depthTexture->imageView };
    m_guiRenderTargetForPresenting->imagelessFramebufferAttachments = { swapchainImageContext.mockColorTexture->imageView };

    // We shouldn't (can't) use the existing data from the swapchain image, so we set current layout accordingly
    swapchainImageContext.mockColorTexture->currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    swapchainImageContext.depthTexture->currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    // If we wrote any timestamps last time we processed this FrameContext, read and validate those results now
    if (frameContext.numTimestampsWrittenLastTime > 0) {
        VkResult timestampGetQueryResults = vkGetQueryPoolResults(device(), frameContext.timestampQueryPool, 0, frameContext.numTimestampsWrittenLastTime, sizeof(frameContext.timestampResults), frameContext.timestampResults, sizeof(TimestampResult64), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        if (timestampGetQueryResults == VK_SUCCESS || timestampGetQueryResults == VK_NOT_READY) {
            // Validate that all timestamps that we have written to have valid results ready to read
            for (uint32_t startIdx = 0; startIdx < frameContext.numTimestampsWrittenLastTime; startIdx += 2) {
                uint32_t endIdx = startIdx + 1;
                if (frameContext.timestampResults[startIdx].available == 0 || frameContext.timestampResults[endIdx].available == 0) {
                    LogError("VulkanBackend: timestamps not available (this probably shouldn't happen?)\n");
                }
            }
        }
    }

    auto elapsedSecondsBetweenTimestamps = [&](uint32_t startIdx, uint32_t endIdx) -> double {
        if (startIdx >= frameContext.numTimestampsWrittenLastTime || endIdx >= frameContext.numTimestampsWrittenLastTime)
            return NAN;
        uint64_t timestampDiff = frameContext.timestampResults[endIdx].timestamp - frameContext.timestampResults[startIdx].timestamp;
        float nanosecondDiff = float(timestampDiff) * m_physicalDeviceProperties.limits.timestampPeriod;
        return double(nanosecondDiff) / (1000.0 * 1000.0 * 1000.0);
    };

    // Draw frame
    {
        uint32_t nextTimestampQueryIdx = 0;

        uint32_t frameStartTimestampIdx = nextTimestampQueryIdx++;
        uint32_t frameEndTimestampIdx = nextTimestampQueryIdx++;
        double gpuFrameElapsedTime = elapsedSecondsBetweenTimestamps(frameStartTimestampIdx, frameEndTimestampIdx);
        m_frameTimer.reportGpuTime(gpuFrameElapsedTime);

        VkCommandBufferBeginInfo commandBufferBeginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        commandBufferBeginInfo.flags = 0u;
        commandBufferBeginInfo.pInheritanceInfo = nullptr;

        VkCommandBuffer commandBuffer = frameContext.commandBuffer;
        if (vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo) != VK_SUCCESS) {
            LogError("VulkanBackend: error beginning command buffer command!\n");
        }

        m_currentlyExecutingMainCommandBuffer = true;

        UploadBuffer& uploadBuffer = *frameContext.uploadBuffer;
        uploadBuffer.reset();

        Registry& registry = *m_pipelineRegistry;
        VulkanCommandList cmdList { *this, commandBuffer };

        vkCmdResetQueryPool(commandBuffer, frameContext.timestampQueryPool, 0, FrameContext::TimestampQueryPoolCount);
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, frameContext.timestampQueryPool, frameStartTimestampIdx);

        ImGui::Begin("Nodes (in order)");
        {
            std::string frameTimePerfString = m_frameTimer.createFormattedString();
            ImGui::Text("Frame time: %s", frameTimePerfString.c_str());

            renderPipeline.forEachNodeInResolvedOrder(registry, [&](const std::string& nodeName, AvgElapsedTimer& nodeTimer, const RenderPipelineNode::ExecuteCallback& nodeExecuteCallback) {
                std::string nodeTimePerfString = nodeTimer.createFormattedString();
                std::string nodeTitle = fmt::format("{} | {}", nodeName, nodeTimePerfString);
                ImGui::CollapsingHeader(nodeTitle.c_str(), ImGuiTreeNodeFlags_Leaf);

                SCOPED_PROFILE_ZONE_DYNAMIC(nodeName, 0x00ffff);
                double cpuStartTime = glfwGetTime();

                // NOTE: This works assuming we never modify the list of nodes (add/remove/reorder)
                uint32_t nodeStartTimestampIdx = nextTimestampQueryIdx++;
                uint32_t nodeEndTimestampIdx = nextTimestampQueryIdx++;
                nodeTimer.reportGpuTime(elapsedSecondsBetweenTimestamps(nodeStartTimestampIdx, nodeEndTimestampIdx));

                vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, frameContext.timestampQueryPool, nodeStartTimestampIdx);

                cmdList.beginDebugLabel(nodeName);
                nodeExecuteCallback(appState, cmdList, uploadBuffer);
                cmdList.endNode({});
                cmdList.endDebugLabel();

                vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, frameContext.timestampQueryPool, nodeEndTimestampIdx);

                double cpuElapsed = glfwGetTime() - cpuStartTime;
                nodeTimer.reportCpuTime(cpuElapsed);
            });
        }
        ImGui::End();

        cmdList.beginDebugLabel("GUI");
        {
            SCOPED_PROFILE_ZONE_BACKEND_NAMED("GUI Rendering");

            ImGui::Render();
            renderDearImguiFrame(commandBuffer, frameContext, swapchainImageContext);
        }
        cmdList.endDebugLabel();

        VulkanTexture& swapchainTexture = *swapchainImageContext.mockColorTexture;
        if (swapchainTexture.currentLayout != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {

            // Performing explicit swapchain layout transition. This should only happen if we don't render any GUI.

            VkImageMemoryBarrier imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            imageBarrier.oldLayout = swapchainTexture.currentLayout;
            imageBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

            imageBarrier.image = swapchainTexture.image;
            imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imageBarrier.subresourceRange.baseMipLevel = 0;
            imageBarrier.subresourceRange.levelCount = 1;
            imageBarrier.subresourceRange.baseArrayLayer = 0;
            imageBarrier.subresourceRange.layerCount = 1;

            // Wait for all color attachment writes ...
            VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            imageBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

            // ... before allowing it can be read (by the OS I guess)
            VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            imageBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

            vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0,
                                 0, nullptr,
                                 0, nullptr,
                                 1, &imageBarrier);
        }

        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, frameContext.timestampQueryPool, frameEndTimestampIdx);
        frameContext.numTimestampsWrittenLastTime = nextTimestampQueryIdx;
        ASSERT(frameContext.numTimestampsWrittenLastTime < FrameContext::TimestampQueryPoolCount);

        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
            LogError("VulkanBackend: error ending command buffer command!\n");
        }

        m_currentlyExecutingMainCommandBuffer = false;
    }

    // Submit queue
    {
        VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };

        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &frameContext.commandBuffer;

        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &frameContext.imageAvailableSemaphore;
        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        submitInfo.pWaitDstStageMask = waitStages;

        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &frameContext.renderingFinishedSemaphore;

        if (vkResetFences(device(), 1, &frameContext.frameFence) != VK_SUCCESS) {
            LogError("VulkanBackend: error resetting in-flight frame fence.\n");
        }

        VkResult submitStatus = vkQueueSubmit(m_graphicsQueue.queue, 1, &submitInfo, frameContext.frameFence);
        if (submitStatus != VK_SUCCESS) {
            LogError("VulkanBackend: could not submit the graphics queue.\n");
        }
    }

    // Present results (synced on the semaphores)
    {
        VkPresentInfoKHR presentInfo = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };

        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &frameContext.renderingFinishedSemaphore;

        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &m_swapchain;
        presentInfo.pImageIndices = &swapchainImageIndex;

        VkResult presentResult = vkQueuePresentKHR(m_presentQueue.queue, &presentInfo);

        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR || s_unhandledWindowResize) {
            recreateSwapchain();
            reconstructRenderPipelineResources(renderPipeline);
        } else if (presentResult != VK_SUCCESS) {
            LogError("VulkanBackend: could not present swapchain (frame %u).\n", m_currentFrameIndex);
        }
    }

    m_currentFrameIndex += 1;
    m_relativeFrameIndex += 1;

    double cpuFrameElapsedTime = glfwGetTime() - cpuFrameStartTime;
    m_frameTimer.reportCpuTime(cpuFrameElapsedTime);

    return true;
}

Registry& VulkanBackend::getPersistentRegistry()
{
    ASSERT(m_persistentRegistry);
    return *m_persistentRegistry;
}

void VulkanBackend::renderPipelineDidChange(RenderPipeline& renderPipeline)
{
    reconstructRenderPipelineResources(renderPipeline);
}

void VulkanBackend::shadersDidRecompile(const std::vector<std::string>& shaderNames, RenderPipeline& renderPipeline)
{
    // Maybe figure out what nodes needs updating and only reconstruct that node & nodes depending on it?
    // On the other hand, creatating these resources should be very fast anyway so maybe shouldn't bother.
    if (shaderNames.size() > 0) {
        reconstructRenderPipelineResources(renderPipeline);
    }
}

void VulkanBackend::reconstructRenderPipelineResources(RenderPipeline& renderPipeline)
{
    SCOPED_PROFILE_ZONE_BACKEND();

    size_t numFrameManagers = m_frameContexts.size();
    ASSERT(numFrameManagers == NumInFlightFrames);

    // We use imageless framebuffers for this one so it doesn't matter that we don't construct the render pipeline knowing the exact images.
    const RenderTarget& templateWindowRenderTarget = *m_clearingRenderTarget;

    Registry* previousRegistry = m_pipelineRegistry.get();
    Registry* registry = new Registry(*this, previousRegistry, &templateWindowRenderTarget);

    renderPipeline.constructAll(*registry);

    m_pipelineRegistry.reset(registry);

    m_relativeFrameIndex = 0;
}

bool VulkanBackend::issueSingleTimeCommand(const std::function<void(VkCommandBuffer)>& callback) const
{
    if (m_currentlyExecutingMainCommandBuffer && vulkanVerboseDebugMessages)
        LogWarning("Issuing single-time command while also \"inside\" the main command buffer. This will cause a stall which "
                   "can be avoided by e.g. using UploadBuffer to stage multiple uploads and copy them over on one go.\n");

    VkCommandBufferAllocateInfo commandBufferAllocInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    commandBufferAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandBufferAllocInfo.commandPool = m_transientCommandPool;
    commandBufferAllocInfo.commandBufferCount = 1;

    VkCommandBuffer oneTimeCommandBuffer;
    vkAllocateCommandBuffers(device(), &commandBufferAllocInfo, &oneTimeCommandBuffer);
    AtScopeExit cleanUpOneTimeUseBuffer([&] {
        vkFreeCommandBuffers(device(), m_transientCommandPool, 1, &oneTimeCommandBuffer);
    });

    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(oneTimeCommandBuffer, &beginInfo) != VK_SUCCESS) {
        LogError("VulkanBackend: could not begin the command buffer.\n");
        return false;
    }

    callback(oneTimeCommandBuffer);

    if (vkEndCommandBuffer(oneTimeCommandBuffer) != VK_SUCCESS) {
        LogError("VulkanBackend: could not end the command buffer.\n");
        return false;
    }

    VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &oneTimeCommandBuffer;

    if (vkQueueSubmit(m_graphicsQueue.queue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
        LogError("VulkanBackend: could not submit the single-time command buffer.\n");
        return false;
    }
    if (vkQueueWaitIdle(m_graphicsQueue.queue) != VK_SUCCESS) {
        LogError("VulkanBackend: error while waiting for the graphics queue to idle.\n");
        return false;
    }

    return true;
}

bool VulkanBackend::copyBuffer(VkBuffer source, VkBuffer destination, size_t size, size_t dstOffset, VkCommandBuffer* commandBuffer) const
{
    VkBufferCopy bufferCopyRegion = {};
    bufferCopyRegion.size = size;
    bufferCopyRegion.srcOffset = 0;
    bufferCopyRegion.dstOffset = dstOffset;

    if (commandBuffer) {
        vkCmdCopyBuffer(*commandBuffer, source, destination, 1, &bufferCopyRegion);
    } else {
        bool success = issueSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
            vkCmdCopyBuffer(commandBuffer, source, destination, 1, &bufferCopyRegion);
        });
        if (!success) {
            LogError("VulkanBackend: error copying buffer, refer to issueSingleTimeCommand errors for more information.\n");
            return false;
        }
    }

    return true;
}

bool VulkanBackend::setBufferMemoryUsingMapping(VmaAllocation allocation, const uint8_t* data, size_t size, size_t offset)
{
    SCOPED_PROFILE_ZONE_BACKEND();

    if (size == 0) {
        return true;
    }

    void* mappedMemory;
    if (vmaMapMemory(globalAllocator(), allocation, &mappedMemory) != VK_SUCCESS) {
        LogError("VulkanBackend: could not map staging buffer.\n");
        return false;
    }

    uint8_t* dst = ((uint8_t*)mappedMemory) + offset;
    std::memcpy(dst, data, size);

    vmaUnmapMemory(globalAllocator(), allocation);

    return true;
}

bool VulkanBackend::setBufferDataUsingStagingBuffer(VkBuffer buffer, const uint8_t* data, size_t size, size_t offset, VkCommandBuffer* commandBuffer)
{
    SCOPED_PROFILE_ZONE_BACKEND();

    if (size == 0) {
        return true;
    }

    VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferCreateInfo.size = size;

    VmaAllocationCreateInfo allocCreateInfo = {};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;
    if (vmaCreateBuffer(globalAllocator(), &bufferCreateInfo, &allocCreateInfo, &stagingBuffer, &stagingAllocation, nullptr) != VK_SUCCESS) {
        LogError("VulkanBackend: could not create staging buffer.\n");
    }

    AtScopeExit cleanUpStagingBuffer([&] {
        vmaDestroyBuffer(globalAllocator(), stagingBuffer, stagingAllocation);
    });

    if (!setBufferMemoryUsingMapping(stagingAllocation, data, size, 0)) {
        LogError("VulkanBackend: could set staging buffer memory.\n");
        return false;
    }

    if (!copyBuffer(stagingBuffer, buffer, size, offset, commandBuffer)) {
        LogError("VulkanBackend: could not copy from staging buffer to buffer.\n");
        return false;
    }

    return true;
}

std::optional<VkPushConstantRange> VulkanBackend::getPushConstantRangeForShader(const Shader& shader) const
{
    SCOPED_PROFILE_ZONE_BACKEND();

    std::optional<VkPushConstantRange> pushConstantRange;

    for (auto& file : shader.files()) {

        VkShaderStageFlags stageFlag;
        switch (file.type()) {
        case ShaderFileType::Vertex:
            stageFlag = VK_SHADER_STAGE_VERTEX_BIT;
            break;
        case ShaderFileType::Fragment:
            stageFlag = VK_SHADER_STAGE_FRAGMENT_BIT;
            break;
        case ShaderFileType::Compute:
            stageFlag = VK_SHADER_STAGE_COMPUTE_BIT;
            break;
        case ShaderFileType::RTRaygen:
            stageFlag = VK_SHADER_STAGE_RAYGEN_BIT_NV;
            break;
        case ShaderFileType::RTClosestHit:
            stageFlag = VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV;
            break;
        case ShaderFileType::RTAnyHit:
            stageFlag = VK_SHADER_STAGE_ANY_HIT_BIT_NV;
            break;
        case ShaderFileType::RTMiss:
            stageFlag = VK_SHADER_STAGE_MISS_BIT_NV;
            break;
        case ShaderFileType::RTIntersection:
            stageFlag = VK_SHADER_STAGE_INTERSECTION_BIT_NV;
            break;
        default:
            ASSERT_NOT_REACHED();
        }

        const auto& spv = ShaderManager::instance().spirv(file);
        spirv_cross::Compiler compiler { spv };
        spirv_cross::ShaderResources resources = compiler.get_shader_resources();

        if (!resources.push_constant_buffers.empty()) {
            ASSERT(resources.push_constant_buffers.size() == 1);
            const spirv_cross::Resource& res = resources.push_constant_buffers[0];
            const spirv_cross::SPIRType& type = compiler.get_type(res.type_id);
            size_t pushConstantSize = compiler.get_declared_struct_size(type);

            if (!pushConstantRange.has_value()) {
                VkPushConstantRange range {};
                range.stageFlags = stageFlag;
                range.size = (uint32_t)pushConstantSize;
                range.offset = 0;
                pushConstantRange = range;
            } else {
                if (pushConstantRange.value().size != pushConstantSize) {
                    LogErrorAndExit("Different push constant sizes in the different shader files!\n");
                }
                pushConstantRange.value().stageFlags |= stageFlag;
            }
        }
    }

    return pushConstantRange;
}

std::pair<std::vector<VkDescriptorSetLayout>, std::optional<VkPushConstantRange>> VulkanBackend::createDescriptorSetLayoutForShader(const Shader& shader) const
{
    SCOPED_PROFILE_ZONE_BACKEND();

    uint32_t maxSetId = 0;
    std::unordered_map<uint32_t, std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding>> sets;

    std::optional<VkPushConstantRange> pushConstantRange;

    for (auto& file : shader.files()) {

        VkShaderStageFlags stageFlag;
        switch (file.type()) {
        case ShaderFileType::Vertex:
            stageFlag = VK_SHADER_STAGE_VERTEX_BIT;
            break;
        case ShaderFileType::Fragment:
            stageFlag = VK_SHADER_STAGE_FRAGMENT_BIT;
            break;
        case ShaderFileType::Compute:
            stageFlag = VK_SHADER_STAGE_COMPUTE_BIT;
            break;
        case ShaderFileType::RTRaygen:
            stageFlag = VK_SHADER_STAGE_RAYGEN_BIT_NV;
            break;
        case ShaderFileType::RTClosestHit:
            stageFlag = VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV;
            break;
        case ShaderFileType::RTAnyHit:
            stageFlag = VK_SHADER_STAGE_ANY_HIT_BIT_NV;
            break;
        case ShaderFileType::RTMiss:
            stageFlag = VK_SHADER_STAGE_MISS_BIT_NV;
            break;
        case ShaderFileType::RTIntersection:
            stageFlag = VK_SHADER_STAGE_INTERSECTION_BIT_NV;
            break;
        default:
            ASSERT_NOT_REACHED();
        }

        const auto& spv = ShaderManager::instance().spirv(file);
        spirv_cross::Compiler compiler { spv };
        spirv_cross::ShaderResources resources = compiler.get_shader_resources();

        auto add = [&](const spirv_cross::Resource& res, VkDescriptorType descriptorType) {
            uint32_t setId = compiler.get_decoration(res.id, spv::Decoration::DecorationDescriptorSet);
            auto& set = sets[setId];

            maxSetId = std::max(maxSetId, setId);

            uint32_t bindingId = compiler.get_decoration(res.id, spv::Decoration::DecorationBinding);
            auto entry = set.find(bindingId);
            if (entry == set.end()) {

                uint32_t arrayCount = 1; // i.e. not an array
                const spirv_cross::SPIRType& type = compiler.get_type(res.type_id);
                if (!type.array.empty()) {
                    ASSERT(type.array.size() == 1); // i.e. no multidimensional arrays
                    arrayCount = type.array[0];
                }

                VkDescriptorSetLayoutBinding binding {};
                binding.binding = bindingId;
                binding.stageFlags = stageFlag;
                binding.descriptorCount = arrayCount;
                binding.descriptorType = descriptorType;
                binding.pImmutableSamplers = nullptr;

                set[bindingId] = binding;

            } else {
                set[bindingId].stageFlags |= stageFlag;
            }
        };

        for (auto& ubo : resources.uniform_buffers) {
            add(ubo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        }
        for (auto& sbo : resources.storage_buffers) {
            add(sbo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        }
        for (auto& sampledImage : resources.sampled_images) {
            add(sampledImage, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        }
        for (auto& storageImage : resources.storage_images) {
            add(storageImage, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        }
        for (auto& accelerationStructure : resources.acceleration_structures) {
            add(accelerationStructure, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV);
        }

        if (!resources.push_constant_buffers.empty()) {
            ASSERT(resources.push_constant_buffers.size() == 1);
            const spirv_cross::Resource& res = resources.push_constant_buffers[0];
            const spirv_cross::SPIRType& type = compiler.get_type(res.type_id);
            size_t pushConstantSize = compiler.get_declared_struct_size(type);

            if (!pushConstantRange.has_value()) {
                VkPushConstantRange range {};
                range.stageFlags = stageFlag;
                range.size = (uint32_t)pushConstantSize;
                range.offset = 0;
                pushConstantRange = range;
            } else {
                if (pushConstantRange.value().size != pushConstantSize) {
                    LogErrorAndExit("Different push constant sizes in the different shader files!\n");
                }
                pushConstantRange.value().stageFlags |= stageFlag;
            }
        }
    }

    std::vector<VkDescriptorSetLayout> setLayouts { (size_t)maxSetId + 1 };
    for (uint32_t setId = 0; setId <= maxSetId; ++setId) {

        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        std::vector<VkDescriptorSetLayoutBinding> layoutBindings {};

        // There can be no gaps in the list of set layouts when creating a pipeline layout, so we fill them in here
        descriptorSetLayoutCreateInfo.bindingCount = 0;
        descriptorSetLayoutCreateInfo.pBindings = nullptr;

        auto entry = sets.find(setId);
        if (entry != sets.end()) {

            for (auto& [id, binding] : entry->second) {
                layoutBindings.push_back(binding);
            }

            descriptorSetLayoutCreateInfo.bindingCount = (uint32_t)layoutBindings.size();
            descriptorSetLayoutCreateInfo.pBindings = layoutBindings.data();
        }

        if (vkCreateDescriptorSetLayout(device(), &descriptorSetLayoutCreateInfo, nullptr, &setLayouts[setId]) != VK_SUCCESS) {
            LogErrorAndExit("Error trying to create descriptor set layout from shader\n");
        }
    }

    return { setLayouts, pushConstantRange };
}

std::vector<VulkanBackend::PushConstantInfo> VulkanBackend::identifyAllPushConstants(const Shader& shader) const
{
    SCOPED_PROFILE_ZONE_BACKEND();

    std::vector<VulkanBackend::PushConstantInfo> infos;

    for (auto& file : shader.files()) {

        ShaderStage stageFlag;
        switch (file.type()) {
        case ShaderFileType::Vertex:
            stageFlag = ShaderStageVertex;
            break;
        case ShaderFileType::Fragment:
            stageFlag = ShaderStageFragment;
            break;
        case ShaderFileType::Compute:
            stageFlag = ShaderStageCompute;
            break;
        case ShaderFileType::RTRaygen:
            stageFlag = ShaderStageRTRayGen;
            break;
        case ShaderFileType::RTClosestHit:
            stageFlag = ShaderStageRTClosestHit;
            break;
        case ShaderFileType::RTAnyHit:
            stageFlag = ShaderStageRTAnyHit;
            break;
        case ShaderFileType::RTMiss:
            stageFlag = ShaderStageRTMiss;
            break;
        case ShaderFileType::RTIntersection:
            stageFlag = ShaderStageRTIntersection;
            break;
        default:
            ASSERT_NOT_REACHED();
        }

        const auto& spv = ShaderManager::instance().spirv(file);
        spirv_cross::Compiler compiler { spv };
        spirv_cross::ShaderResources resources = compiler.get_shader_resources();

        if (!resources.push_constant_buffers.empty()) {
            ASSERT(resources.push_constant_buffers.size() == 1);

            const spirv_cross::Resource& pc_res = resources.push_constant_buffers[0];
            const spirv_cross::SPIRType& pc_type = compiler.get_type(pc_res.type_id);

            // With the NAMED_UNIFORMS macro all push constant blocks will contain exactly one struct with named members
            if (pc_type.member_types.size() != 1) {
                LogErrorAndExit("identifyAllPushConstants: please use the NAMED_UNIFORMS macro to define push constants!");
            }

            const spirv_cross::TypeID& struct_type_id = pc_type.member_types[0];
            const spirv_cross::SPIRType& struct_type = compiler.get_type(struct_type_id);
            if (struct_type.basetype != spirv_cross::SPIRType::Struct) {
                LogErrorAndExit("identifyAllPushConstants: please use the NAMED_UNIFORMS macro to define push constants!");
            }

            size_t memberCount = struct_type.member_types.size();
            if (infos.size() > 0 && infos.size() != memberCount) {
                LogErrorAndExit("identifyAllPushConstants: mismatch in push constant layout (different member counts!)!");
            }

            for (int i = 0; i < memberCount; ++i) {

                const std::string& member_name = compiler.get_member_name(struct_type_id, i);
                size_t offset = compiler.type_struct_member_offset(struct_type, i);
                size_t size = compiler.get_declared_struct_member_size(struct_type, i);

                if (infos.size() == i) {
                    VulkanBackend::PushConstantInfo info;
                    info.name = member_name;
                    info.stages = stageFlag;
                    info.offset = (uint32_t)offset;
                    info.size = (int32_t)size;

                    infos.push_back(info);
                }
                else {
                    // We've already seen push constants in another shader file, so just verify there is no mismatch
                    VulkanBackend::PushConstantInfo& existing = infos[i];
                    if (existing.name != member_name || existing.offset != offset || existing.size != size) {
                        LogErrorAndExit("identifyAllPushConstants: mismatch in push constant layout!");
                    } else {
                        existing.stages = ShaderStage(existing.stages | stageFlag);
                    }
                }

            }
        }
    }

    return infos;
}

uint32_t VulkanBackend::findAppropriateMemory(uint32_t typeBits, VkMemoryPropertyFlags properties) const
{
    SCOPED_PROFILE_ZONE_BACKEND();

    VkPhysicalDeviceMemoryProperties memoryProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice(), &memoryProperties);

    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++) {
        // Is type i at all supported, given the typeBits?
        if (!(typeBits & (1u << i))) {
            continue;
        }

        if ((memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    LogErrorAndExit("VulkanBackend: could not find any appropriate memory, exiting.\n");
}
