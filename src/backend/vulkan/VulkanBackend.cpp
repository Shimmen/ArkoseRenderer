#include "VulkanBackend.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "VulkanCommandList.h"
#include "backend/vulkan/VulkanResources.h"
#include "rendering/Registry.h"
#include "rendering/ShaderManager.h"
#include "utility/FileIO.h"
#include "utility/GlobalState.h"
#include "utility/Logging.h"
#include "utility/util.h"
#include <ImGuizmo.h>
#include <algorithm>
#include <cstring>
#include <fmt/format.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <spirv_cross.hpp>
#include <unordered_map>
#include <unordered_set>

static bool s_unhandledWindowResize = false;

VulkanBackend::VulkanBackend(GLFWwindow* window, App& app)
    : m_window(window)
    , m_app(app)
{
    m_sceneRegistry = std::make_unique<Registry>(*this);
    app.createScene(badge(), *m_sceneRegistry);

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    GlobalState::getMutable(badge()).updateWindowExtent({ width, height });
    glfwSetFramebufferSizeCallback(window, static_cast<GLFWframebuffersizefun>([](GLFWwindow* window, int width, int height) {
                                       GlobalState::getMutable(badge()).updateWindowExtent({ width, height });
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

        auto dbgMessengerCreateInfo = debugMessengerCreateInfo();
        m_instance = createInstance(requestedLayers, &dbgMessengerCreateInfo);
        m_messenger = createDebugMessenger(m_instance, &dbgMessengerCreateInfo);

    } else {
        m_instance = createInstance(requestedLayers, nullptr);
    }

    if (glfwCreateWindowSurface(m_instance, window, nullptr, &m_surface) != VK_SUCCESS)
        LogErrorAndExit("VulkanBackend: can't create window surface, exiting.\n");

    m_physicalDevice = pickBestPhysicalDevice();
    findQueueFamilyIndices(m_physicalDevice, m_surface);

    {
        uint32_t extensionCount;
        vkEnumerateDeviceExtensionProperties(physicalDevice(), nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> availableExtensions { extensionCount };
        vkEnumerateDeviceExtensionProperties(physicalDevice(), nullptr, &extensionCount, availableExtensions.data());
        for (auto& ext : availableExtensions)
            m_availableExtensions.insert(ext.extensionName);
    }

    if (!collectAndVerifyCapabilitySupport(app))
        LogErrorAndExit("VulkanBackend: could not verify support for all capabilities required by the app\n");

    m_device = createDevice(requestedLayers, m_physicalDevice);

    vkGetDeviceQueue(m_device, m_presentQueue.familyIndex, 0, &m_presentQueue.queue);
    vkGetDeviceQueue(m_device, m_graphicsQueue.familyIndex, 0, &m_graphicsQueue.queue);
    vkGetDeviceQueue(m_device, m_computeQueue.familyIndex, 0, &m_computeQueue.queue);

    createSemaphoresAndFences(device());

    if (VulkanRTX::isSupportedOnPhysicalDevice(physicalDevice())) {
        m_rtx = std::make_unique<VulkanRTX>(*this, physicalDevice(), device());
    }

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = physicalDevice();
    allocatorInfo.device = device();
    allocatorInfo.flags = 0u;
    if (vmaCreateAllocator(&allocatorInfo, &m_memoryAllocator) != VK_SUCCESS) {
        LogErrorAndExit("VulkanBackend::VulkanBackend(): could not create memory allocator, exiting.\n");
    }

    VkCommandPoolCreateInfo poolCreateInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    poolCreateInfo.queueFamilyIndex = m_graphicsQueue.familyIndex;
    poolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; // (so we can easily reuse them each frame)
    if (vkCreateCommandPool(device(), &poolCreateInfo, nullptr, &m_renderGraphFrameCommandPool) != VK_SUCCESS) {
        LogErrorAndExit("VulkanBackend::VulkanBackend(): could not create command pool for the graphics queue, exiting.\n");
    }

    VkCommandPoolCreateInfo transientPoolCreateInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    transientPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    transientPoolCreateInfo.queueFamilyIndex = m_graphicsQueue.familyIndex;
    if (vkCreateCommandPool(device(), &transientPoolCreateInfo, nullptr, &m_transientCommandPool) != VK_SUCCESS) {
        LogErrorAndExit("VulkanBackend::VulkanBackend(): could not create transient command pool, exiting.\n");
    }

    size_t numEvents = 4;
    m_events.resize(numEvents);
    VkEventCreateInfo eventCreateInfo = { VK_STRUCTURE_TYPE_EVENT_CREATE_INFO };
    for (size_t i = 0; i < numEvents; ++i) {
        if (vkCreateEvent(device(), &eventCreateInfo, nullptr, &m_events[i]) != VK_SUCCESS) {
            LogErrorAndExit("VulkanBackend::VulkanBackend(): could not create event, exiting.\n");
        }
        if (vkSetEvent(device(), m_events[i]) != VK_SUCCESS) {
            LogErrorAndExit("VulkanBackend::VulkanBackend(): could not signal event after creating it, exiting.\n");
        }
    }

    createAndSetupSwapchain(physicalDevice(), device(), m_surface);
    createWindowRenderTargetFrontend();

    setupDearImgui();

    m_renderGraph = std::make_unique<RenderGraph>();
    m_app.setup(*m_renderGraph);
    reconstructRenderGraphResources(*m_renderGraph);
}

VulkanBackend::~VulkanBackend()
{
    // Before destroying stuff, make sure it's done with all scheduled work
    vkDeviceWaitIdle(device());

    destroyDearImgui();

    vkFreeCommandBuffers(device(), m_renderGraphFrameCommandPool, m_frameCommandBuffers.size(), m_frameCommandBuffers.data());

    m_frameRegistries.clear();
    m_nodeRegistry.reset();
    m_sceneRegistry.reset();

    destroySwapchain();

    for (VkEvent event : m_events) {
        vkDestroyEvent(device(), event, nullptr);
    }

    vkDestroyCommandPool(device(), m_renderGraphFrameCommandPool, nullptr);
    vkDestroyCommandPool(device(), m_transientCommandPool, nullptr);

    for (size_t it = 0; it < maxFramesInFlight; ++it) {
        vkDestroySemaphore(device(), m_imageAvailableSemaphores[it], nullptr);
        vkDestroySemaphore(device(), m_renderFinishedSemaphores[it], nullptr);
        vkDestroyFence(device(), m_inFlightFrameFences[it], nullptr);
    }

    vmaDestroyAllocator(m_memoryAllocator);

    vkDestroyDevice(m_device, nullptr);
    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);

    if (m_messenger.has_value()) {
        auto destroyFunc = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT");
        ASSERT(destroyFunc != nullptr);
        destroyFunc(m_instance, m_messenger.value(), nullptr);
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

bool VulkanBackend::collectAndVerifyCapabilitySupport(App& app)
{
    VkPhysicalDeviceFeatures2 features2 { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    const VkPhysicalDeviceFeatures& features = features2.features;

    VkPhysicalDeviceDescriptorIndexingFeatures indexingFeatures { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES };
    features2.pNext = &indexingFeatures;

    VkPhysicalDevice16BitStorageFeatures sixteenBitStorageFeatures { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES };
    indexingFeatures.pNext = &sixteenBitStorageFeatures;

    VkPhysicalDeviceShaderFloat16Int8Features shaderSmallTypeFeatures { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES };
    sixteenBitStorageFeatures.pNext = &shaderSmallTypeFeatures;

    vkGetPhysicalDeviceFeatures2(physicalDevice(), &features2);

    auto isSupported = [&](Capability capability) -> bool {
        switch (capability) {
        case Capability::RtxRayTracing:
            return hasSupportForExtension(VK_NV_RAY_TRACING_EXTENSION_NAME);
        case Capability::Shader16BitFloat:
            return hasSupportForExtension(VK_KHR_16BIT_STORAGE_EXTENSION_NAME)
                && hasSupportForExtension(VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME)
                && hasSupportForExtension(VK_KHR_STORAGE_BUFFER_STORAGE_CLASS_EXTENSION_NAME)
                && sixteenBitStorageFeatures.storageInputOutput16 && sixteenBitStorageFeatures.storagePushConstant16
                && sixteenBitStorageFeatures.storageBuffer16BitAccess && sixteenBitStorageFeatures.uniformAndStorageBuffer16BitAccess
                && shaderSmallTypeFeatures.shaderFloat16;
        case Capability::ShaderTextureArrayDynamicIndexing:
            return hasSupportForExtension(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME)
                && features.shaderSampledImageArrayDynamicIndexing && indexingFeatures.shaderSampledImageArrayNonUniformIndexing && indexingFeatures.runtimeDescriptorArray;
        case Capability::ShaderBufferArrayDynamicIndexing:
            return hasSupportForExtension(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME)
                && features.shaderStorageBufferArrayDynamicIndexing && features.shaderUniformBufferArrayDynamicIndexing
                && indexingFeatures.shaderStorageBufferArrayNonUniformIndexing && indexingFeatures.shaderUniformBufferArrayNonUniformIndexing
                && indexingFeatures.runtimeDescriptorArray;
        }
    };

    bool allRequiredSupported = true;

    // First check a few "common" features that are required in all cases
    if (!features.samplerAnisotropy || !features.fillModeNonSolid || !features.fragmentStoresAndAtomics || !features.vertexPipelineStoresAndAtomics) {
        LogError("VulkanBackend: no support for required common device feature\n");
        allRequiredSupported = false;
    }

    for (auto& cap : app.requiredCapabilities()) {
        if (isSupported(cap)) {
            m_activeCapabilities[cap] = true;
        } else {
            LogError("VulkanBackend: no support for required '%s' capability\n", capabilityName(cap).c_str());
            allRequiredSupported = false;
        }
    }

    for (auto& cap : app.optionalCapabilities()) {
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
    return std::make_unique<VulkanRenderTarget>(*this, attachments);
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
                                                              const Shader& shader, std::vector<BindingSet*> bindingSets,
                                                              const Viewport& viewport, const BlendState& blendState, const RasterState& rasterState, const DepthState& depthState)
{
    return std::make_unique<VulkanRenderState>(*this, renderTarget, vertexLayout, shader, bindingSets, viewport, blendState, rasterState, depthState);
}

std::unique_ptr<BottomLevelAS> VulkanBackend::createBottomLevelAccelerationStructure(std::vector<RTGeometry> geometries)
{
    return std::make_unique<VulkanBottomLevelAS>(*this, geometries);
}

std::unique_ptr<TopLevelAS> VulkanBackend::createTopLevelAccelerationStructure(std::vector<RTGeometryInstance> instances)
{
    return std::make_unique<VulkanTopLevelAS>(*this, instances);
}

std::unique_ptr<RayTracingState> VulkanBackend::createRayTracingState(ShaderBindingTable& sbt, std::vector<BindingSet*> bidningSets, uint32_t maxRecursionDepth)
{
    return std::make_unique<VulkanRayTracingState>(*this, sbt, bidningSets, maxRecursionDepth);
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
        validationFeatures.enabledValidationFeatureCount = enabledValidationFeatures.size();
        validationFeatures.pEnabledValidationFeatures = enabledValidationFeatures.data();
    }

    VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    appInfo.pApplicationName = "ArkoseRenderer";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0); // NOLINT(hicpp-signed-bitwise)
    appInfo.pEngineName = "ArkoseRendererEngine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0); // NOLINT(hicpp-signed-bitwise)
    appInfo.apiVersion = VK_API_VERSION_1_1; // NOLINT(hicpp-signed-bitwise)

    VkInstanceCreateInfo instanceCreateInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    instanceCreateInfo.pApplicationInfo = &appInfo;

    instanceCreateInfo.enabledExtensionCount = instanceExtensions.size();
    instanceCreateInfo.ppEnabledExtensionNames = instanceExtensions.data();

    instanceCreateInfo.enabledLayerCount = requestedLayers.size();
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

    VkPhysicalDeviceFeatures features = {};
    VkPhysicalDeviceDescriptorIndexingFeatures indexingFeatures { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES };
    VkPhysicalDevice16BitStorageFeatures sixteenBitStorageFeatures { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES };
    VkPhysicalDeviceShaderFloat16Int8Features shaderSmallTypeFeatures { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES };

    // Enable some "common" features expected to exist
    features.samplerAnisotropy = VK_TRUE;
    features.fillModeNonSolid = VK_TRUE;
    features.fragmentStoresAndAtomics = VK_TRUE;
    features.vertexPipelineStoresAndAtomics = VK_TRUE;

    for (auto& [capability, active] : m_activeCapabilities) {
        if (!active)
            continue;
        switch (capability) {
        case Capability::RtxRayTracing:
            deviceExtensions.push_back(VK_NV_RAY_TRACING_EXTENSION_NAME);
            break;
        case Capability::Shader16BitFloat:
            deviceExtensions.push_back(VK_KHR_16BIT_STORAGE_EXTENSION_NAME);
            deviceExtensions.push_back(VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME);
            deviceExtensions.push_back(VK_KHR_STORAGE_BUFFER_STORAGE_CLASS_EXTENSION_NAME);
            sixteenBitStorageFeatures.storageInputOutput16 = VK_TRUE;
            sixteenBitStorageFeatures.storagePushConstant16 = VK_TRUE;
            sixteenBitStorageFeatures.storageBuffer16BitAccess = VK_TRUE;
            sixteenBitStorageFeatures.uniformAndStorageBuffer16BitAccess = VK_TRUE;
            shaderSmallTypeFeatures.shaderFloat16 = VK_TRUE;
            break;
        case Capability::ShaderTextureArrayDynamicIndexing:
            deviceExtensions.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
            features.shaderSampledImageArrayDynamicIndexing = VK_TRUE;
            indexingFeatures.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
            indexingFeatures.runtimeDescriptorArray = VK_TRUE;
            break;
        case Capability::ShaderBufferArrayDynamicIndexing:
            deviceExtensions.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
            features.shaderStorageBufferArrayDynamicIndexing = VK_TRUE;
            features.shaderUniformBufferArrayDynamicIndexing = VK_TRUE;
            indexingFeatures.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
            indexingFeatures.shaderUniformBufferArrayNonUniformIndexing = VK_TRUE;
            indexingFeatures.runtimeDescriptorArray = VK_TRUE;
            break;
        default:
            ASSERT_NOT_REACHED();
        }
    }

    VkDeviceCreateInfo deviceCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };

    deviceCreateInfo.queueCreateInfoCount = queueCreateInfos.size();
    deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();

    // (the support of these requestedLayers should already have been checked)
    deviceCreateInfo.enabledLayerCount = requestedLayers.size();
    deviceCreateInfo.ppEnabledLayerNames = requestedLayers.data();

    deviceCreateInfo.enabledExtensionCount = deviceExtensions.size();
    deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();

    deviceCreateInfo.pEnabledFeatures = &features;

    // Device features extension chain
    deviceCreateInfo.pNext = &indexingFeatures;
    indexingFeatures.pNext = &sixteenBitStorageFeatures;
    sixteenBitStorageFeatures.pNext = &shaderSmallTypeFeatures;

    VkDevice device;
    if (vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device) != VK_SUCCESS)
        LogErrorAndExit("VulkanBackend: could not create a device, exiting.\n");

    return device;
}

void VulkanBackend::findQueueFamilyIndices(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface)
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
        LogErrorAndExit("VulkanBackend::findQueueFamilyIndices(): could not find a graphics queue, exiting.\n");
    }
    if (!foundComputeQueue) {
        LogErrorAndExit("VulkanBackend::findQueueFamilyIndices(): could not find a compute queue, exiting.\n");
    }
    if (!foundPresentQueue) {
        LogErrorAndExit("VulkanBackend::findQueueFamilyIndices(): could not find a present queue, exiting.\n");
    }
}

VkBool32 VulkanBackend::debugMessageCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageTypes,
                                             const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
{
    LogError("Vulkan debug message; %s\n", pCallbackData->pMessage);
    return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT VulkanBackend::debugMessengerCreateInfo() const
{
    VkDebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
    debugMessengerCreateInfo.pfnUserCallback = debugMessageCallback;
    debugMessengerCreateInfo.pUserData = nullptr;

    debugMessengerCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT; // NOLINT(hicpp-signed-bitwise)
    if (vulkanVerboseDebugMessages)
        debugMessengerCreateInfo.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT;
    debugMessengerCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT; // NOLINT(hicpp-signed-bitwise)

    return debugMessengerCreateInfo;
}

VkDebugUtilsMessengerEXT VulkanBackend::createDebugMessenger(VkInstance instance, VkDebugUtilsMessengerCreateInfoEXT* createInfo) const
{
    auto createFunc = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (!createFunc) {
        LogErrorAndExit("VulkanBackend::createDebugMessenger(): could not get function 'vkCreateDebugUtilsMessengerEXT', exiting.\n");
    }

    VkDebugUtilsMessengerEXT messenger;
    if (createFunc(instance, createInfo, nullptr, &messenger) != VK_SUCCESS) {
        LogErrorAndExit("VulkanBackend::createDebugMessenger(): could not create the debug messenger, exiting.\n");
    }

    return messenger;
}

VkPhysicalDevice VulkanBackend::pickBestPhysicalDevice() const
{
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

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physicalDevice, &props);
    LogInfo("VulkanBackend: using physical device '%s'\n", props.deviceName);

    return physicalDevice;
}

void VulkanBackend::createSemaphoresAndFences(VkDevice device)
{
    VkSemaphoreCreateInfo semaphoreCreateInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

    VkFenceCreateInfo fenceCreateInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    bool allSemaphoresCreatedSuccessfully = true;
    bool allFencesCreatedSuccessfully = true;

    for (size_t it = 0; it < maxFramesInFlight; ++it) {
        if (vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &m_imageAvailableSemaphores[it]) != VK_SUCCESS) {
            allSemaphoresCreatedSuccessfully = false;
            break;
        }
        if (vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &m_renderFinishedSemaphores[it]) != VK_SUCCESS) {
            allSemaphoresCreatedSuccessfully = false;
            break;
        }
        if (vkCreateFence(device, &fenceCreateInfo, nullptr, &m_inFlightFrameFences[it]) != VK_SUCCESS) {
            allFencesCreatedSuccessfully = false;
            break;
        }
    }

    if (!allSemaphoresCreatedSuccessfully) {
        LogErrorAndExit("VulkanBackend::createSemaphoresAndFences(): could not create one or more semaphores, exiting.\n");
    }
    if (!allFencesCreatedSuccessfully) {
        LogErrorAndExit("VulkanBackend::createSemaphoresAndFences(): could not create one or more fence, exiting.\n");
    }
}

void VulkanBackend::createAndSetupSwapchain(VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface)
{
    VkSurfaceCapabilitiesKHR surfaceCapabilities;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCapabilities) != VK_SUCCESS) {
        LogErrorAndExit("VulkanBackend::createAndSetupSwapchain(): could not get surface capabilities, exiting.\n");
    }

    VkSwapchainCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    createInfo.surface = surface;

    // Request one more image than required, if possible (see https://github.com/KhronosGroup/Vulkan-Docs/issues/909 for information)
    createInfo.minImageCount = surfaceCapabilities.minImageCount + 1;
    if (surfaceCapabilities.minImageCount != 0) {
        // (max of zero means no upper limit, so don't clamp in that case)
        createInfo.minImageCount = std::min(createInfo.minImageCount, surfaceCapabilities.maxImageCount);
    }

    VkSurfaceFormatKHR surfaceFormat = pickBestSurfaceFormat();
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;

    VkPresentModeKHR presentMode = pickBestPresentMode();
    createInfo.presentMode = presentMode;

    VkExtent2D swapchainExtent = pickBestSwapchainExtent();
    createInfo.imageExtent = swapchainExtent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT; // TODO: What do we want here? Maybe this suffices?
    // TODO: Assure VK_IMAGE_USAGE_STORAGE_BIT is supported using vkGetPhysicalDeviceSurfaceCapabilitiesKHR & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT

    if (vulkanDebugMode) {
        // for nsight debugging & similar stuff)
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
        LogErrorAndExit("VulkanBackend::createAndSetupSwapchain(): could not create swapchain, exiting.\n");
    }

    vkGetSwapchainImagesKHR(device, m_swapchain, &m_numSwapchainImages, nullptr);
    m_swapchainImages.resize(m_numSwapchainImages);
    vkGetSwapchainImagesKHR(device, m_swapchain, &m_numSwapchainImages, m_swapchainImages.data());

    m_swapchainImageViews.resize(m_numSwapchainImages);
    for (size_t i = 0; i < m_swapchainImages.size(); ++i) {

        VkImageViewCreateInfo imageViewCreateInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };

        imageViewCreateInfo.image = m_swapchainImages[i];
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

        if (vkCreateImageView(device, &imageViewCreateInfo, nullptr, &m_swapchainImageViews[i]) != VK_SUCCESS) {
            LogErrorAndExit("VulkanBackend::createAndSetupSwapchain(): could not create image view %u (out of %u), exiting.\n", i, m_numSwapchainImages);
        }
    }

    m_swapchainExtent = { swapchainExtent.width, swapchainExtent.height };
    m_swapchainImageFormat = surfaceFormat.format;

    Texture::TextureDescription depthDesc {
        .type = Texture::Type::Texture2D,
        .extent = m_swapchainExtent,
        .format = Texture::Format::Depth32F,
        .minFilter = Texture::MinFilter::Nearest,
        .magFilter = Texture::MagFilter::Nearest,
        .wrapMode = {
            Texture::WrapMode::Repeat,
            Texture::WrapMode::Repeat,
            Texture::WrapMode::Repeat },
        .mipmap = Texture::Mipmap::None,
        .multisampling = Texture::Multisampling::None
    };
    m_swapchainDepthTexture = std::make_unique<VulkanTexture>(*this, depthDesc);
    setupWindowRenderTargets();

    if (m_guiIsSetup) {
        ImGui_ImplVulkan_SetMinImageCount(m_numSwapchainImages);
        updateDearImguiFramebuffers();
    }

    // Create main command buffers, one per swapchain image
    m_frameCommandBuffers.resize(m_numSwapchainImages);
    {
        VkCommandBufferAllocateInfo commandBufferAllocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        commandBufferAllocateInfo.commandPool = m_renderGraphFrameCommandPool;
        commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; // (can be submitted to a queue for execution, but cannot be called from other command buffers)
        commandBufferAllocateInfo.commandBufferCount = m_frameCommandBuffers.size();

        if (vkAllocateCommandBuffers(device, &commandBufferAllocateInfo, m_frameCommandBuffers.data()) != VK_SUCCESS) {
            LogErrorAndExit("VulkanBackend::createAndSetupSwapchain(): could not create the main command buffers, exiting.\n");
        }
    }
}

void VulkanBackend::destroySwapchain()
{
    m_swapchainDepthTexture.reset();

    // TODO: Could this be rewritten to only use our existing deleteRenderTarget method?
    vkDestroyRenderPass(device(), m_swapchainMockRenderTargets[0]->compatibleRenderPass, nullptr);
    for (std::unique_ptr<VulkanRenderTarget>& renderTarget : m_swapchainMockRenderTargets) {
        vkDestroyFramebuffer(device(), renderTarget->framebuffer, nullptr);
        renderTarget->framebuffer = VK_NULL_HANDLE;
        renderTarget->compatibleRenderPass = VK_NULL_HANDLE;
        renderTarget.reset();
    }

    for (size_t it = 0; it < m_numSwapchainImages; ++it) {
        vkDestroyImageView(device(), m_swapchainImageViews[it], nullptr);
    }

    vkDestroySwapchainKHR(device(), m_swapchain, nullptr);
}

Extent2D VulkanBackend::recreateSwapchain()
{
    while (true) {
        // As long as we are minimized, don't do anything
        int windowFramebufferWidth, windowFramebufferHeight;
        glfwGetFramebufferSize(m_window, &windowFramebufferWidth, &windowFramebufferHeight);
        if (windowFramebufferWidth == 0 || windowFramebufferHeight == 0) {
            LogInfo("VulkanBackend::recreateSwapchain(): rendering paused since there are no pixels to draw to.\n");
            glfwWaitEvents();
        } else {
            LogInfo("VulkanBackend::recreateSwapchain(): rendering resumed.\n");
            break;
        }
    }

    vkDeviceWaitIdle(device());

    destroySwapchain();
    createAndSetupSwapchain(physicalDevice(), device(), m_surface);
    createWindowRenderTargetFrontend();

    s_unhandledWindowResize = false;

    return m_swapchainExtent;
}

void VulkanBackend::createWindowRenderTargetFrontend()
{
    ASSERT(m_numSwapchainImages > 0);

    m_swapchainMockColorTextures.resize(m_numSwapchainImages);
    m_swapchainMockRenderTargets.resize(m_numSwapchainImages);

    for (size_t i = 0; i < m_numSwapchainImages; ++i) {

        auto colorTexture = std::make_unique<VulkanTexture>();
        {
            colorTexture->m_type = Texture::Type::Texture2D;
            colorTexture->m_extent = m_swapchainExtent;
            colorTexture->m_format = Texture::Format::Unknown;
            colorTexture->m_minFilter = Texture::MinFilter::Nearest;
            colorTexture->m_magFilter = Texture::MagFilter::Nearest;
            colorTexture->m_wrapMode = {
                Texture::WrapMode::Repeat,
                Texture::WrapMode::Repeat,
                Texture::WrapMode::Repeat
            };
            colorTexture->m_mipmap = Texture::Mipmap::None;
            colorTexture->m_multisampling = Texture::Multisampling::None;

            colorTexture->vkFormat = m_swapchainImageFormat;
            colorTexture->image = m_swapchainImages[i];
            colorTexture->imageView = m_swapchainImageViews[i];
            colorTexture->currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        }
        m_swapchainMockColorTextures[i] = std::move(colorTexture);

        auto renderTarget = std::make_unique<VulkanRenderTarget>();
        {
            renderTarget->m_colorAttachments = { { RenderTarget::AttachmentType::Color0, m_swapchainMockColorTextures[i].get() } };
            renderTarget->m_depthAttachment = { RenderTarget::AttachmentType::Depth, m_swapchainDepthTexture.get() };

            renderTarget->m_multisampling = Texture::Multisampling::None;
            renderTarget->m_extent = m_swapchainExtent;

            renderTarget->compatibleRenderPass = m_swapchainRenderPass;
            renderTarget->framebuffer = m_swapchainFramebuffers[i];
            renderTarget->attachedTextures = {
                { m_swapchainMockColorTextures[i].get(), VK_IMAGE_LAYOUT_PRESENT_SRC_KHR }, // this (layout) is important so that we know that we don't need to do an explicit transition before presenting
                { m_swapchainDepthTexture.get(), VK_IMAGE_LAYOUT_UNDEFINED } // (this (layout) probably doesn't matter for the depth image)
            };
        }
        m_swapchainMockRenderTargets[i] = std::move(renderTarget);
    }
}

void VulkanBackend::setupDearImgui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    //ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    //

    ImGui_ImplGlfw_InitForVulkan(m_window, true);

    //

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

    //

    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format = m_swapchainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef = {};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = VK_NULL_HANDLE;

    // TODO: Is this needed here??
    // Setup subpass dependency to make sure we have the right stuff before drawing to a swapchain image.
    // see https://vulkan-tutorial.com/en/Drawing_a_triangle/Drawing/Rendering_and_presentation for info.
    VkSubpassDependency subpassDependency = {};
    subpassDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    subpassDependency.dstSubpass = 0; // i.e. the first and only subpass we have here
    subpassDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    subpassDependency.srcAccessMask = 0;
    subpassDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    subpassDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassCreateInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    renderPassCreateInfo.attachmentCount = 1;
    renderPassCreateInfo.pAttachments = &colorAttachment;
    renderPassCreateInfo.subpassCount = 1;
    renderPassCreateInfo.pSubpasses = &subpass;
    renderPassCreateInfo.dependencyCount = 1;
    renderPassCreateInfo.pDependencies = &subpassDependency;

    if (vkCreateRenderPass(device(), &renderPassCreateInfo, nullptr, &m_guiRenderPass) != VK_SUCCESS) {
        LogErrorAndExit("DearImGui error while setting up render pass\n");
    }

    //

    updateDearImguiFramebuffers();

    //

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

    initInfo.MinImageCount = m_numSwapchainImages; // (todo: should this be something different than the actual count??)
    initInfo.ImageCount = m_numSwapchainImages;

    initInfo.DescriptorPool = m_guiDescriptorPool;
    initInfo.PipelineCache = VK_NULL_HANDLE;

    ImGui_ImplVulkan_Init(&initInfo, m_guiRenderPass);

    //

    issueSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
        ImGui_ImplVulkan_CreateFontsTexture(commandBuffer);
    });
    ImGui_ImplVulkan_DestroyFontUploadObjects();

    //

    m_guiIsSetup = true;
}

void VulkanBackend::destroyDearImgui()
{
    vkDestroyDescriptorPool(device(), m_guiDescriptorPool, nullptr);
    vkDestroyRenderPass(device(), m_guiRenderPass, nullptr);
    for (VkFramebuffer framebuffer : m_guiFramebuffers) {
        vkDestroyFramebuffer(device(), framebuffer, nullptr);
    }

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    m_guiIsSetup = false;
}

void VulkanBackend::updateDearImguiFramebuffers()
{
    for (VkFramebuffer& framebuffer : m_guiFramebuffers) {
        vkDestroyFramebuffer(device(), framebuffer, nullptr);
    }
    m_guiFramebuffers.clear();

    for (uint32_t idx = 0; idx < m_numSwapchainImages; ++idx) {
        VkFramebufferCreateInfo framebufferCreateInfo = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        framebufferCreateInfo.renderPass = m_guiRenderPass;
        framebufferCreateInfo.attachmentCount = 1;
        framebufferCreateInfo.pAttachments = &m_swapchainImageViews[idx];
        framebufferCreateInfo.width = m_swapchainExtent.width();
        framebufferCreateInfo.height = m_swapchainExtent.height();
        framebufferCreateInfo.layers = 1;

        VkFramebuffer framebuffer;
        if (vkCreateFramebuffer(device(), &framebufferCreateInfo, nullptr, &framebuffer) != VK_SUCCESS) {
            LogErrorAndExit("DearImGui error while setting up framebuffer\n");
        }
        m_guiFramebuffers.push_back(framebuffer);
    }
}

void VulkanBackend::renderDearImguiFrame(VkCommandBuffer commandBuffer, uint32_t swapchainImageIndex)
{
    VkRenderPassBeginInfo passBeginInfo = {};
    passBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    passBeginInfo.renderPass = m_guiRenderPass;
    passBeginInfo.framebuffer = m_guiFramebuffers[swapchainImageIndex];
    passBeginInfo.renderArea.extent.width = m_swapchainExtent.width();
    passBeginInfo.renderArea.extent.height = m_swapchainExtent.height();
    passBeginInfo.clearValueCount = 0;
    passBeginInfo.pClearValues = nullptr;

    vkCmdBeginRenderPass(commandBuffer, &passBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
    vkCmdEndRenderPass(commandBuffer);

    VulkanTexture& swapchainTexture = *m_swapchainMockColorTextures[swapchainImageIndex];
    swapchainTexture.currentLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
}

bool VulkanBackend::executeFrame(double elapsedTime, double deltaTime)
{
    uint32_t currentFrameMod = m_currentFrameIndex % maxFramesInFlight;

    if (vkWaitForFences(device(), 1, &m_inFlightFrameFences[currentFrameMod], VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        LogError("VulkanBackend::executeFrame(): error while waiting for in-flight frame fence (frame %u).\n", m_currentFrameIndex);
    }

    AppState appState { m_swapchainExtent, deltaTime, elapsedTime, m_currentFrameIndex };

    uint32_t swapchainImageIndex;
    VkResult acquireResult = vkAcquireNextImageKHR(device(), m_swapchain, UINT64_MAX, m_imageAvailableSemaphores[currentFrameMod], VK_NULL_HANDLE, &swapchainImageIndex);

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        // Since we couldn't acquire an image to draw to, recreate the swapchain and report that it didn't work
        Extent2D newWindowExtent = recreateSwapchain();
        appState = appState.updateWindowExtent(newWindowExtent);
        reconstructRenderGraphResources(*m_renderGraph);
        return false;
    }
    if (acquireResult == VK_SUBOPTIMAL_KHR) {
        // Since we did manage to acquire an image, just roll with it for now, but it will probably resolve itself after presenting
        LogWarning("VulkanBackend::executeFrame(): next image was acquired but it's suboptimal, ignoring.\n");
    }

    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        LogError("VulkanBackend::executeFrame(): error acquiring next swapchain image.\n");
    }

    // We shouldn't use the data from the swapchain image, so we set current layout accordingly (not sure about depth, but sure..)
    VulkanTexture& currentColorTexture = *m_swapchainMockColorTextures[swapchainImageIndex];
    currentColorTexture.currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    m_swapchainDepthTexture->currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    drawFrame(appState, elapsedTime, deltaTime, swapchainImageIndex);

    submitQueue(swapchainImageIndex, &m_imageAvailableSemaphores[currentFrameMod], &m_renderFinishedSemaphores[currentFrameMod], &m_inFlightFrameFences[currentFrameMod]);

    // Present results (synced on the semaphores)
    {
        VkPresentInfoKHR presentInfo = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };

        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &m_renderFinishedSemaphores[currentFrameMod];

        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &m_swapchain;
        presentInfo.pImageIndices = &swapchainImageIndex;

        VkResult presentResult = vkQueuePresentKHR(m_presentQueue.queue, &presentInfo);

        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR || s_unhandledWindowResize) {
            recreateSwapchain();
            reconstructRenderGraphResources(*m_renderGraph);
        } else if (presentResult != VK_SUCCESS) {
            LogError("VulkanBackend::executeFrame(): could not present swapchain (frame %u).\n", m_currentFrameIndex);
        }
    }

    m_currentFrameIndex += 1;
    return true;
}

void VulkanBackend::drawFrame(const AppState& appState, double elapsedTime, double deltaTime, uint32_t swapchainImageIndex)
{
    ASSERT(m_renderGraph);

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    m_app.update(float(elapsedTime), float(deltaTime));

    VkCommandBufferBeginInfo commandBufferBeginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    commandBufferBeginInfo.flags = 0u;
    commandBufferBeginInfo.pInheritanceInfo = nullptr;

    VkCommandBuffer commandBuffer = m_frameCommandBuffers[swapchainImageIndex];
    if (vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo) != VK_SUCCESS) {
        LogError("VulkanBackend::executeRenderGraph(): error beginning command buffer command!\n");
    }

    Registry& associatedRegistry = *m_frameRegistries[swapchainImageIndex];
    VulkanCommandList cmdList { *this, commandBuffer };

    ImGui::Begin("Nodes (in order)");
    m_renderGraph->forEachNodeInResolvedOrder(associatedRegistry, [&](const std::string& nodeName, NodeTimer& nodeTimer, const RenderGraphNode::ExecuteCallback& nodeExecuteCallback) {
        double cpuTime = nodeTimer.averageCpuTime() * 1000.0;
        std::string title = isnan(cpuTime)
            ? fmt::format("{} | CPU: - ms", nodeName)
            : fmt::format("{} | CPU: {:.2f} ms", nodeName, cpuTime);
        ImGui::CollapsingHeader(title.c_str(), ImGuiTreeNodeFlags_Leaf);

        double cpuStartTime = glfwGetTime();

        nodeExecuteCallback(appState, cmdList);

        double cpuElapsed = glfwGetTime() - cpuStartTime;
        nodeTimer.reportCpuTime(cpuElapsed);

        cmdList.endNode({});
    });
    ImGui::End();

    {
        static ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;

        auto& input = Input::instance();
        if (input.wasKeyPressed(Key::T))
            operation = ImGuizmo::TRANSLATE;
        else if (input.wasKeyPressed(Key::R))
            operation = ImGuizmo::ROTATE;
        else if (input.wasKeyPressed(Key::Y))
            operation = ImGuizmo::SCALE;

        Model* selectedModel = m_app.scene().selectedModel();
        if (selectedModel) {

            ImGuizmo::BeginFrame();
            ImGuizmo::SetRect(0, 0, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);

            // FIXME: Support world transforms! Well, we don't really have hierarchies right now, so it doesn't really matter.
            //  What we do have is meshes with their own transform under a model, and we are modifying the model's transform here.
            //  Maybe in the future we want to be able to modify meshes too?
            ImGuizmo::MODE mode = ImGuizmo::LOCAL;

            mat4 viewMatrix = m_app.scene().camera().viewMatrix();
            mat4 projMatrix = m_app.scene().camera().projectionMatrix();

            // Silly stuff, since ImGuizmo doesn't seem to like my projection matrix..
            projMatrix.y = -projMatrix.y;

            mat4 matrix = selectedModel->transform().localMatrix();
            ImGuizmo::Manipulate(value_ptr(viewMatrix), value_ptr(projMatrix), operation, mode, value_ptr(matrix));
            selectedModel->transform().setLocalMatrix(matrix);
        }
    }

    ImGui::Render();
    renderDearImguiFrame(commandBuffer, swapchainImageIndex);
    ImGui::UpdatePlatformWindows();

    // Explicitly transfer the swapchain image to a present layout if not already
    // In most cases it should always be, but with nsight it seems to do weird things.
    VulkanTexture& swapchainTexture = *m_swapchainMockColorTextures[swapchainImageIndex];
    if (swapchainTexture.currentLayout != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        transitionImageLayout(swapchainTexture.image, false, swapchainTexture.currentLayout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, &commandBuffer);
        LogInfo("VulkanBackend::executeRenderGraph(): performing explicit swapchain layout transition. "
                "This should only happen if we don't render to the window and don't draw any GUI.\n");
    }

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        LogError("VulkanBackend::executeRenderGraph(): error ending command buffer command!\n");
    }
}

void VulkanBackend::setupWindowRenderTargets()
{
    // TODO: Could this be rewritten to use the common functions?
    //  I.e. createRenderTarget, passing in the manually created color textures etc.
    //  The only "problem" is probably the specific layouts (VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)

    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format = m_swapchainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depthAttachment = {};
    depthAttachment.format = m_swapchainDepthTexture->vkFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    std::array<VkAttachmentDescription, 2> allAttachments = {
        colorAttachment,
        depthAttachment
    };

    VkAttachmentReference colorAttachmentRef = {};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef = {};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    // Setup subpass dependency to make sure we have the right stuff before drawing to a swapchain image.
    // see https://vulkan-tutorial.com/en/Drawing_a_triangle/Drawing/Rendering_and_presentation for info.
    VkSubpassDependency subpassDependency = {};
    subpassDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    subpassDependency.dstSubpass = 0; // i.e. the first and only subpass we have here
    subpassDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    subpassDependency.srcAccessMask = 0;
    subpassDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    subpassDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassCreateInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    renderPassCreateInfo.attachmentCount = allAttachments.size();
    renderPassCreateInfo.pAttachments = allAttachments.data();
    renderPassCreateInfo.subpassCount = 1;
    renderPassCreateInfo.pSubpasses = &subpass;
    renderPassCreateInfo.dependencyCount = 1;
    renderPassCreateInfo.pDependencies = &subpassDependency;

    VkRenderPass renderPass {};
    if (vkCreateRenderPass(device(), &renderPassCreateInfo, nullptr, &renderPass) != VK_SUCCESS) {
        LogErrorAndExit("Error trying to create window render pass\n");
    }
    m_swapchainRenderPass = renderPass;

    m_swapchainFramebuffers.resize(m_numSwapchainImages);
    for (size_t it = 0; it < m_numSwapchainImages; ++it) {

        std::array<VkImageView, 2> attachmentImageViews = {
            m_swapchainImageViews[it],
            m_swapchainDepthTexture->imageView
        };

        VkFramebufferCreateInfo framebufferCreateInfo = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        framebufferCreateInfo.renderPass = renderPass;
        framebufferCreateInfo.attachmentCount = attachmentImageViews.size();
        framebufferCreateInfo.pAttachments = attachmentImageViews.data();
        framebufferCreateInfo.width = m_swapchainExtent.width();
        framebufferCreateInfo.height = m_swapchainExtent.height();
        framebufferCreateInfo.layers = 1;

        VkFramebuffer framebuffer;
        if (vkCreateFramebuffer(device(), &framebufferCreateInfo, nullptr, &framebuffer) != VK_SUCCESS) {
            LogErrorAndExit("Error trying to create window framebuffer\n");
        }

        m_swapchainFramebuffers[it] = framebuffer;
    }
}

void VulkanBackend::reconstructRenderGraphResources(RenderGraph& renderGraph)
{
    uint32_t numFrameManagers = m_numSwapchainImages;

    // Create new resource managers
    auto nodeRegistry = std::make_unique<Registry>(*this);
    std::vector<std::unique_ptr<Registry>> frameRegistries {};
    for (uint32_t i = 0; i < numFrameManagers; ++i) {
        const RenderTarget& windowRenderTargetForFrame = *m_swapchainMockRenderTargets[i];
        frameRegistries.push_back(std::make_unique<Registry>(*this, &windowRenderTargetForFrame));
    }

    // TODO: Fix me, this is stupid..
    std::vector<Registry*> regPointers {};
    regPointers.reserve(frameRegistries.size());
    for (auto& mng : frameRegistries) {
        regPointers.emplace_back(mng.get());
    }

    renderGraph.constructAll(*nodeRegistry, regPointers);

    // First create & replace node resources
    //replaceResourcesForRegistry(m_nodeRegistry.get(), nodeRegistry.get());
    m_nodeRegistry = std::move(nodeRegistry);

    // Then create & replace frame resources
    m_frameRegistries.resize(numFrameManagers);
    for (uint32_t i = 0; i < numFrameManagers; ++i) {
        //replaceResourcesForRegistry(m_frameRegistries[i].get(), frameRegistries[i].get());
        m_frameRegistries[i] = std::move(frameRegistries[i]);
    }
}

bool VulkanBackend::issueSingleTimeCommand(const std::function<void(VkCommandBuffer)>& callback) const
{
    VkCommandBufferAllocateInfo commandBufferAllocInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    commandBufferAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandBufferAllocInfo.commandPool = m_transientCommandPool;
    commandBufferAllocInfo.commandBufferCount = 1;

    VkCommandBuffer oneTimeCommandBuffer;
    vkAllocateCommandBuffers(device(), &commandBufferAllocInfo, &oneTimeCommandBuffer);
    AT_SCOPE_EXIT([&] {
        vkFreeCommandBuffers(device(), m_transientCommandPool, 1, &oneTimeCommandBuffer);
    });

    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(oneTimeCommandBuffer, &beginInfo) != VK_SUCCESS) {
        LogError("VulkanBackend::issueSingleTimeCommand(): could not begin the command buffer.\n");
        return false;
    }

    callback(oneTimeCommandBuffer);

    if (vkEndCommandBuffer(oneTimeCommandBuffer) != VK_SUCCESS) {
        LogError("VulkanBackend::issueSingleTimeCommand(): could not end the command buffer.\n");
        return false;
    }

    VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &oneTimeCommandBuffer;

    if (vkQueueSubmit(m_graphicsQueue.queue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
        LogError("VulkanBackend::issueSingleTimeCommand(): could not submit the single-time command buffer.\n");
        return false;
    }
    if (vkQueueWaitIdle(m_graphicsQueue.queue) != VK_SUCCESS) {
        LogError("VulkanBackend::issueSingleTimeCommand(): error while waiting for the graphics queue to idle.\n");
        return false;
    }

    return true;
}

bool VulkanBackend::copyBuffer(VkBuffer source, VkBuffer destination, VkDeviceSize size, VkCommandBuffer* commandBuffer) const
{
    VkBufferCopy bufferCopyRegion = {};
    bufferCopyRegion.size = size;
    bufferCopyRegion.srcOffset = 0;
    bufferCopyRegion.dstOffset = 0;

    if (commandBuffer) {
        vkCmdCopyBuffer(*commandBuffer, source, destination, 1, &bufferCopyRegion);
    } else {
        bool success = issueSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
            vkCmdCopyBuffer(commandBuffer, source, destination, 1, &bufferCopyRegion);
        });
        if (!success) {
            LogError("VulkanBackend::copyBuffer(): error copying buffer, refer to issueSingleTimeCommand errors for more information.\n");
            return false;
        }
    }

    return true;
}

bool VulkanBackend::setBufferMemoryUsingMapping(VmaAllocation allocation, const void* data, VkDeviceSize size)
{
    if (size == 0) {
        return true;
    }

    void* mappedMemory;
    if (vmaMapMemory(globalAllocator(), allocation, &mappedMemory) != VK_SUCCESS) {
        LogError("VulkanBackend::setBufferMemoryUsingMapping(): could not map staging buffer.\n");
        return false;
    }
    std::memcpy(mappedMemory, data, size);
    vmaUnmapMemory(globalAllocator(), allocation);
    return true;
}

bool VulkanBackend::setBufferDataUsingStagingBuffer(VkBuffer buffer, const void* data, VkDeviceSize size, VkCommandBuffer* commandBuffer)
{
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
        LogError("VulkanBackend::setBufferDataUsingStagingBuffer(): could not create staging buffer.\n");
    }

    AT_SCOPE_EXIT([&] {
        vmaDestroyBuffer(globalAllocator(), stagingBuffer, stagingAllocation);
    });

    if (!setBufferMemoryUsingMapping(stagingAllocation, data, size)) {
        LogError("VulkanBackend::setBufferDataUsingStagingBuffer(): could set staging buffer memory.\n");
        return false;
    }

    if (!copyBuffer(stagingBuffer, buffer, size, commandBuffer)) {
        LogError("VulkanBackend::setBufferDataUsingStagingBuffer(): could not copy from staging buffer to buffer.\n");
        return false;
    }

    return true;
}

bool VulkanBackend::transitionImageLayout(VkImage image, bool isDepthFormat, VkImageLayout oldLayout, VkImageLayout newLayout, VkCommandBuffer* currentCommandBuffer) const
{
    if (oldLayout == newLayout) {
        LogWarning("VulkanBackend::transitionImageLayout(): old & new layout identical, ignoring.\n");
        return true;
    }

    VkImageMemoryBarrier imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    imageBarrier.oldLayout = oldLayout;
    imageBarrier.newLayout = newLayout;
    imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    imageBarrier.image = image;
    imageBarrier.subresourceRange.aspectMask = isDepthFormat ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    imageBarrier.subresourceRange.baseMipLevel = 0;
    imageBarrier.subresourceRange.levelCount = 1;
    imageBarrier.subresourceRange.baseArrayLayer = 0;
    imageBarrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    // TODO: This whole function needs to be scrapped, really..

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        imageBarrier.srcAccessMask = 0;

        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        imageBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    } else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {

        // Wait for all color attachment writes ...
        sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        imageBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        // ... before allowing any shaders to read the memory
        destinationStage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    } else if (oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {

        // Wait for all color attachment writes ...
        sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        imageBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        // ... before allowing any shaders to read the memory
        destinationStage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    } else if (oldLayout == VK_IMAGE_LAYOUT_GENERAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {

        // Wait for all memory writes ...
        sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        imageBarrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;

        // ... before allowing any shaders to read the memory
        destinationStage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_GENERAL) {

        // Wait for all shader memory reads...
        sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        imageBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;

        // ... before allowing any memory writes
        destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;

    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {

        // Wait for all color attachment writes ...
        sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        imageBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        // ... before allowing any reading or writing
        destinationStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

    } else {
        LogErrorAndExit("VulkanBackend::transitionImageLayout(): old & new layout combination unsupported by application, exiting.\n");
    }

    if (currentCommandBuffer) {
        vkCmdPipelineBarrier(*currentCommandBuffer, sourceStage, destinationStage, 0,
                             0, nullptr,
                             0, nullptr,
                             1, &imageBarrier);
    } else {
        bool success = issueSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
            vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0,
                                 0, nullptr,
                                 0, nullptr,
                                 1, &imageBarrier);
        });
        if (!success) {
            LogError("VulkanBackend::transitionImageLayout(): error transitioning layout, refer to issueSingleTimeCommand errors for more information.\n");
            return false;
        }
    }

    return true;
}

bool VulkanBackend::copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height, bool isDepthImage) const
{
    VkBufferImageCopy region = {};
    region.bufferOffset = 0;

    // (zeros here indicate tightly packed data)
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageOffset = VkOffset3D { 0, 0, 0 };
    region.imageExtent = VkExtent3D { width, height, 1 };

    region.imageSubresource.aspectMask = isDepthImage ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;

    bool success = issueSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
        // TODO/NOTE: This assumes that the image we are copying to has the VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL layout!
        vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    });

    if (!success) {
        LogError("VulkanBackend::copyBufferToImage(): error copying buffer to image, refer to issueSingleTimeCommand errors for more information.\n");
        return false;
    }

    return true;
}

std::pair<std::vector<VkDescriptorSetLayout>, std::optional<VkPushConstantRange>> VulkanBackend::createDescriptorSetLayoutForShader(const Shader& shader) const
{
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

        const auto& spv = ShaderManager::instance().spirv(file.path());
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
                range.size = pushConstantSize;
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

    std::vector<VkDescriptorSetLayout> setLayouts { maxSetId + 1 };
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

            descriptorSetLayoutCreateInfo.bindingCount = layoutBindings.size();
            descriptorSetLayoutCreateInfo.pBindings = layoutBindings.data();
        }

        if (vkCreateDescriptorSetLayout(device(), &descriptorSetLayoutCreateInfo, nullptr, &setLayouts[setId]) != VK_SUCCESS) {
            LogErrorAndExit("Error trying to create descriptor set layout from shader\n");
        }
    }

    return { setLayouts, pushConstantRange };
}

uint32_t VulkanBackend::findAppropriateMemory(uint32_t typeBits, VkMemoryPropertyFlags properties) const
{
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

    LogErrorAndExit("VulkanBackend::findAppropriateMemory(): could not find any appropriate memory, exiting.\n");
}

void VulkanBackend::submitQueue(uint32_t imageIndex, VkSemaphore* waitFor, VkSemaphore* signal, VkFence* inFlight)
{
    VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };

    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitFor;
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_frameCommandBuffers[imageIndex];

    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signal;

    if (vkResetFences(device(), 1, inFlight) != VK_SUCCESS) {
        LogError("VulkanBackend::submitQueue(): error resetting in-flight frame fence (index %u).\n", imageIndex);
    }

    VkResult submitStatus = vkQueueSubmit(m_graphicsQueue.queue, 1, &submitInfo, *inFlight);
    if (submitStatus != VK_SUCCESS) {
        LogError("VulkanBackend::submitQueue(): could not submit the graphics queue (index %u).\n", imageIndex);
    }
}
