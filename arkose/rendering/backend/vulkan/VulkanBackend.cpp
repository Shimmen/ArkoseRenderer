#include "VulkanBackend.h"

#include "core/CommandLine.h"
#include "rendering/backend/vulkan/VulkanBindingSet.h"
#include "rendering/backend/vulkan/VulkanBuffer.h"
#include "rendering/backend/vulkan/VulkanCommandList.h"
#include "rendering/backend/vulkan/VulkanComputeState.h"
#include "rendering/backend/vulkan/VulkanRenderState.h"
#include "rendering/backend/vulkan/VulkanRenderTarget.h"
#include "rendering/backend/vulkan/VulkanSampler.h"
#include "rendering/backend/vulkan/VulkanTexture.h"
#include "rendering/backend/vulkan/extensions/ray-tracing-khr/VulkanAccelerationStructureKHR.h"
#include "rendering/backend/vulkan/extensions/ray-tracing-khr/VulkanRayTracingStateKHR.h"
#include "rendering/backend/shader/Shader.h"
#include "rendering/backend/shader/ShaderManager.h"
#include "system/System.h"
#include <ark/defer.h>
#include "rendering/Registry.h"
#include "rendering/RenderPipeline.h"
#include "utility/FileIO.h"
#include "core/Logging.h"
#include "utility/Profiling.h"
#include "core/Assert.h"
#include <algorithm>
#include <cstring>
#include <imgui.h>
#include <implot.h>
#include <ark/conversion.h>
#include <backends/imgui_impl_vulkan.h>
#include <spirv_cross.hpp>
#include <unordered_map>
#include <unordered_set>

#if PLATFORM_LINUX
#include <dlfcn.h> // for dlopen & dlsym (for RenderDoc API loading)
#endif

#if defined(WITH_AFTERMATH_SDK)
#include "GFSDK_Aftermath.h"
#include "GFSDK_Aftermath_GpuCrashDump.h"
static bool AftermathCrashDumpCollectionActive = false;
static void AftermathGpuCrashCallback(void const* gpuCrashDump, u32 gpuCrashDumpSize, void* userData)
{
    std::filesystem::path gpuCrashDumpPath = "Logs/ArkoseGPUCrash.nv-gpudmp";

    ARKOSE_LOG(Info, "VulkanBackend: NVIDIA Nsight Aftermath detected a GPU crash, writing dump to disk at '{}'", gpuCrashDumpPath);
    FileIO::writeBinaryDataToFile(gpuCrashDumpPath, reinterpret_cast<std::byte const*>(gpuCrashDump), gpuCrashDumpSize);
}
static void AftermathGpuCrashShaderInfoCallback(void const* shaderDebugInfo, u32 shaderDebugInfoSize, void* userData)
{
    std::filesystem::path shaderDebugInfoPath = "Logs/ArkoseGPUCrash.nv-debuginfo";

    ARKOSE_LOG(Info, "VulkanBackend: NVIDIA Nsight Aftermath detected a GPU crash, writing shader info to disk at '{}'", shaderDebugInfoPath);
    FileIO::writeBinaryDataToFile(shaderDebugInfoPath, reinterpret_cast<std::byte const*>(shaderDebugInfo), shaderDebugInfoSize);
}
#endif

#if defined(TRACY_ENABLE)
#define SCOPED_PROFILE_ZONE_GPU(commandBuffer, nameLiteral) TracyVkZone(m_tracyVulkanContext, commandBuffer, nameLiteral);
#define SCOPED_PROFILE_ZONE_GPU_DYNAMIC(commandBuffer, nameString) TracyVkZoneTransient(m_tracyVulkanContext, TracyConcat(ScopedProfileZone, nameString), commandBuffer, nameString.c_str(), nameString.size());
#else
#define SCOPED_PROFILE_ZONE_GPU(commandBuffer, nameLiteral)
#define SCOPED_PROFILE_ZONE_GPU_DYNAMIC(commandBuffer, nameString)
#endif

VulkanBackend::VulkanBackend(Badge<Backend>, const AppSpecification& appSpecification)
{
    if constexpr (vulkanDebugMode) {
        if (CommandLine::hasArgument("-renderdoc")) {
            #if PLATFORM_WINDOWS
            // TODO: Don't hard-code renderdoc.dll location. Instead, find it during CMake configuration
            // and copy it to the executable directory. Then we can just check for "renderdoc.dll" to find it.
            if (HMODULE renderdocModule = LoadLibraryA("C:\\Program Files\\RenderDoc\\renderdoc.dll")) {
                pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)GetProcAddress(renderdocModule, "RENDERDOC_GetAPI");
            #elif PLATFORM_LINUX
            if (void* renderdocModule = dlopen("librenderdoc.so", RTLD_NOW)) {
                pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)dlsym(renderdocModule, "RENDERDOC_GetAPI");
            #endif
                int result = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_6_0, (void**)&m_renderdocAPI);
                if (result == 1) {
                    ARKOSE_LOG(Info, "VulkanBackend: RenderDoc overlay enabled");
                } else {
                    ARKOSE_LOG(Error, "VulkanBackend: failed to initialize RenderDoc API ({})", result);
                }
            }
        }
    }

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

    if constexpr (vulkanDebugMode) {
        ARKOSE_LOG(Info, "VulkanBackend: debug mode enabled!");

        ARKOSE_ASSERT(hasSupportForLayer("VK_LAYER_KHRONOS_validation"));
        requestedLayers.emplace_back("VK_LAYER_KHRONOS_validation");

        auto dbgMessengerCreateInfo = VulkanDebugUtils::debugMessengerCreateInfo();
        m_instance = createInstance(requestedLayers, &dbgMessengerCreateInfo);

        m_debugUtils = std::make_unique<VulkanDebugUtils>(*this, m_instance);
        if (debugUtils().vkCreateDebugUtilsMessengerEXT(m_instance, &dbgMessengerCreateInfo, nullptr, &m_debugMessenger) != VK_SUCCESS) {
            ARKOSE_LOG(Fatal, "VulkanBackend: could not create the debug messenger, exiting.");
        }

    } else {
        m_instance = createInstance(requestedLayers, nullptr);
    }

    void* vulkanSufaceUntyped = System::get().createVulkanSurface(m_instance);
    m_surface = static_cast<VkSurfaceKHR>(vulkanSufaceUntyped);

    m_physicalDevice = pickBestPhysicalDevice();
    vkGetPhysicalDeviceProperties(physicalDevice(), &m_physicalDeviceProperties);
    vkGetPhysicalDeviceMemoryProperties(physicalDevice(), &m_physicalDeviceMemoryProperties);
    auto deviceName = std::string(m_physicalDeviceProperties.deviceName);
    ARKOSE_LOG(Info, "VulkanBackend: using physical device '{}'", deviceName);

    m_supportsResizableBAR = checkForResizableBARSupport();
    if (m_supportsResizableBAR) {
        ARKOSE_LOG(Info, "VulkanBackend: Resizable BAR (ReBAR) supported - will avoid staging buffers where applicable");
    } else {
        ARKOSE_LOG(Info, "VulkanBackend: Resizable BAR (ReBAR) not supported");
    }

    findQueueFamilyIndices(m_physicalDevice, m_surface);

    {
        uint32_t extensionCount;
        vkEnumerateDeviceExtensionProperties(physicalDevice(), nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> availableExtensions { extensionCount };
        vkEnumerateDeviceExtensionProperties(physicalDevice(), nullptr, &extensionCount, availableExtensions.data());
        for (auto& ext : availableExtensions)
            m_availableDeviceExtensions.insert(ext.extensionName);
    }

    if (!collectAndVerifyCapabilitySupport(appSpecification))
        ARKOSE_LOG(Fatal, "VulkanBackend: could not verify support for all capabilities required by the app");

    m_device = createDevice(requestedLayers, m_physicalDevice);

    vkGetDeviceQueue(m_device, m_presentQueue.familyIndex, 0, &m_presentQueue.queue);
    vkGetDeviceQueue(m_device, m_graphicsQueue.familyIndex, 0, &m_graphicsQueue.queue);
    vkGetDeviceQueue(m_device, m_computeQueue.familyIndex, 0, &m_computeQueue.queue);

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.vulkanApiVersion = VulkanApiVersion;
    allocatorInfo.instance = m_instance;
    allocatorInfo.physicalDevice = physicalDevice();
    allocatorInfo.device = device();
    allocatorInfo.flags = 0u;
    if (hasActiveCapability(Backend::Capability::RayTracing)) {
        // Device address required if we use ray tracing
        allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    }
    if (hasEnabledDeviceExtension(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME)) {
        // Allow VMA to make use of the memory budget management data available from extension
        allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
    }
    if (vmaCreateAllocator(&allocatorInfo, &m_memoryAllocator) != VK_SUCCESS) {
        ARKOSE_LOG(Fatal, "VulkanBackend: could not create memory allocator, exiting.");
    }

    VkCommandPoolCreateInfo poolCreateInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    poolCreateInfo.queueFamilyIndex = m_graphicsQueue.familyIndex;
    poolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; // (so we can easily reuse them each frame)
    if (vkCreateCommandPool(device(), &poolCreateInfo, nullptr, &m_defaultCommandPool) != VK_SUCCESS) {
        ARKOSE_LOG(Fatal, "VulkanBackend: could not create command pool for the graphics queue, exiting.");
    }

    VkCommandPoolCreateInfo transientPoolCreateInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    transientPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    transientPoolCreateInfo.queueFamilyIndex = m_graphicsQueue.familyIndex;
    if (vkCreateCommandPool(device(), &transientPoolCreateInfo, nullptr, &m_transientCommandPool) != VK_SUCCESS) {
        ARKOSE_LOG(Fatal, "VulkanBackend: could not create transient command pool, exiting.");
    }

    if (hasActiveCapability(Backend::Capability::RayTracing)) {
        m_rayTracingKhr = std::make_unique<VulkanRayTracingKHR>(*this, physicalDevice(), device());
        ARKOSE_LOG(Info, "VulkanBackend: with ray tracing");
        if (hasSupportForDeviceExtension(VK_EXT_OPACITY_MICROMAP_EXTENSION_NAME)) {
            m_opacityMicromapExt = std::make_unique<VulkanOpacityMicromapEXT>(*this, physicalDevice(), device());
            ARKOSE_LOG(Info, "VulkanBackend: with opacity micromaps");
        } else {
            ARKOSE_LOG(Info, "VulkanBackend: without opacity micromaps");
        }
    } else {
        ARKOSE_LOG(Info, "VulkanBackend: no ray tracing");
    }

    if (hasActiveCapability(Backend::Capability::MeshShading)) {
        m_meshShaderExt = std::make_unique<VulkanMeshShaderEXT>(*this, physicalDevice(), device());
    }

    #if WITH_DLSS
    {
        bool runningOnNvidiaPhysicalDevice = m_physicalDeviceProperties.vendorID == 0x10DE;
        if (runningOnNvidiaPhysicalDevice && m_dlssHasAllRequiredExtensions && !m_renderdocAPI) {
            m_dlss = std::make_unique<VulkanDLSS>(*this, m_instance, physicalDevice(), device());
            if (!m_dlss->isReadyToUse()) {
                ARKOSE_LOG(Warning, "VulkanBackend: DLSS is not supported, but all required extensions etc. "
                                    "should be enabled by now. Is the dll placed next to the exe by the build process?");
            }
        }
    }
    if (m_dlss->isReadyToUse()) {
        ARKOSE_LOG(Info, "VulkanBackend: DLSS is ready to use!");
    } else
    #endif
    {
        ARKOSE_LOG(Info, "VulkanBackend: DLSS is not available.");
    }

    #if WITH_NRD
    m_nrd = std::make_unique<VulkanNRD>(*this);
    if (m_nrd->isReadyToUse()) {
        ARKOSE_LOG(Info, "VulkanBackend: NVIDIA Real-time Denoising (NRD) is ready to use!");
    }
    else
    #endif
    {
        ARKOSE_LOG(Info, "VulkanBackend: NVIDIA Real-time Denoising (NRD) is not available.");
    }

    // Create empty stub descriptor set layout (useful for filling gaps as Vulkan doesn't allow having gaps)
    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    descriptorSetLayoutCreateInfo.bindingCount = 0;
    descriptorSetLayoutCreateInfo.pBindings = nullptr;
    if (vkCreateDescriptorSetLayout(device(), &descriptorSetLayoutCreateInfo, nullptr, &m_emptyDescriptorSetLayout) != VK_SUCCESS) {
        ARKOSE_LOG(Fatal, "Error trying to create empty stub descriptor set layout");
    }

    m_pipelineCache = createAndLoadPipelineCacheFromDisk();

    createSwapchain(physicalDevice(), device(), m_surface);
    createFrameContexts();

    #if defined(TRACY_ENABLE)
    {
        VkCommandBufferAllocateInfo commandBufferAllocInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        commandBufferAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandBufferAllocInfo.commandPool = m_defaultCommandPool;
        commandBufferAllocInfo.commandBufferCount = 1;
        vkAllocateCommandBuffers(device(), &commandBufferAllocInfo, &m_tracyCommandBuffer);

        m_tracyVulkanContext = TracyVkContextCalibrated(physicalDevice(), device(), m_graphicsQueue.queue, m_tracyCommandBuffer,
                                                        FetchVulkanInstanceProcAddr(m_instance, vkGetPhysicalDeviceCalibrateableTimeDomainsEXT),
                                                        FetchVulkanDeviceProcAddr(device(), vkGetCalibratedTimestampsEXT));

        const char tracyVulkanContextName[] = "Graphics Queue";
        TracyVkContextName(m_tracyVulkanContext, tracyVulkanContextName, sizeof(tracyVulkanContextName));
    }
    #endif

    setupDearImgui();
}

VulkanBackend::~VulkanBackend()
{
    // Before destroying stuff, make sure we're done with all scheduled work
    completePendingOperations();

    m_pipelineRegistry.reset();

    #if WITH_DLSS
    {
        m_dlss.reset();
    }
    #endif

    m_rayTracingKhr.reset();

    destroyDearImgui();

    #if defined(TRACY_ENABLE)
    {
        TracyVkDestroy(m_tracyVulkanContext);
        vkFreeCommandBuffers(device(), m_defaultCommandPool, 1, &m_tracyCommandBuffer);
    }
    #endif

    destroyFrameContexts();
    destroySwapchain();

    savePipelineCacheToDisk(m_pipelineCache);
    vkDestroyPipelineCache(device(), m_pipelineCache, nullptr);

    vkDestroyDescriptorSetLayout(device(), m_emptyDescriptorSetLayout, nullptr);

    vkDestroyCommandPool(device(), m_defaultCommandPool, nullptr);
    vkDestroyCommandPool(device(), m_transientCommandPool, nullptr);

    vmaDestroyAllocator(m_memoryAllocator);

    #if defined(WITH_AFTERMATH_SDK)
    if (AftermathCrashDumpCollectionActive) {
        GFSDK_Aftermath_DisableGpuCrashDumps();
    }
    #endif

    vkDestroyDevice(m_device, nullptr);
    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);

    if constexpr (vulkanDebugMode) {
        debugUtils().vkDestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
        m_debugUtils.reset();
    }

    vkDestroyInstance(m_instance, nullptr);
}

void VulkanBackend::completePendingOperations()
{
    vkDeviceWaitIdle(device());
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

bool VulkanBackend::hasSupportForDeviceExtension(const std::string& name) const
{
    if (m_physicalDevice == VK_NULL_HANDLE)
        ARKOSE_LOG(Fatal, "Checking support for extension but no physical device exist yet. Maybe you meant to check for instance extensions?");

    auto it = m_availableDeviceExtensions.find(name);
    if (it == m_availableDeviceExtensions.end())
        return false;
    return true;
}

bool VulkanBackend::hasEnabledDeviceExtension(const std::string& name) const
{
    return m_enabledDeviceExtensions.find(name) != m_enabledDeviceExtensions.end();
}

bool VulkanBackend::hasSupportForInstanceExtension(const std::string& name) const
{
    auto it = m_availableInstanceExtensions.find(name);
    if (it == m_availableInstanceExtensions.end())
        return false;
    return true;
}

bool VulkanBackend::hasEnabledInstanceExtension(const std::string& name) const
{
    return m_enabledInstanceExtensions.find(name) != m_enabledInstanceExtensions.end();
}

bool VulkanBackend::collectAndVerifyCapabilitySupport(const AppSpecification& appSpecification)
{
    VkPhysicalDeviceFeatures2 features2 { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    const VkPhysicalDeviceFeatures& features = features2.features;

    VkPhysicalDeviceVulkan11Features vk11features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
    features2.pNext = &vk11features;

    VkPhysicalDeviceVulkan12Features vk12features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    vk11features.pNext = &vk12features;

    VkPhysicalDeviceVulkan13Features vk13features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    vk12features.pNext = &vk13features;

    VkPhysicalDeviceVulkan14Features vk14features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES };
    vk13features.pNext = &vk14features;

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR khrRayTracingPipelineFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR };
    vk14features.pNext = &khrRayTracingPipelineFeatures;

    VkPhysicalDeviceAccelerationStructureFeaturesKHR khrAccelerationStructureFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
    khrRayTracingPipelineFeatures.pNext = &khrAccelerationStructureFeatures;

    VkPhysicalDeviceRayQueryFeaturesKHR khrRayQueryFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR };
    khrAccelerationStructureFeatures.pNext = &khrRayQueryFeatures;

    VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT };
    khrRayQueryFeatures.pNext = &meshShaderFeatures;

    VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR fragmentShaderBarycentricFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_BARYCENTRIC_FEATURES_KHR };
    meshShaderFeatures.pNext = &fragmentShaderBarycentricFeatures;

    VkPhysicalDeviceOpacityMicromapFeaturesEXT opacityMicromapFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_FEATURES_EXT };
    fragmentShaderBarycentricFeatures.pNext = &opacityMicromapFeatures;

    vkGetPhysicalDeviceFeatures2(physicalDevice(), &features2);

    auto isSupported = [&](Capability capability) -> bool {
        switch (capability) {
        case Capability::RayTracing: {
            bool nvidiaRayTracingSupport = hasSupportForDeviceExtension(VK_NV_RAY_TRACING_EXTENSION_NAME);
            bool khrRayTracingSupport =
                hasSupportForDeviceExtension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)
                    && khrRayTracingPipelineFeatures.rayTracingPipeline
                    && khrRayTracingPipelineFeatures.rayTracingPipelineTraceRaysIndirect
                    && khrRayTracingPipelineFeatures.rayTraversalPrimitiveCulling
                && hasSupportForDeviceExtension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)
                    && khrAccelerationStructureFeatures.accelerationStructure
                    //&& khrAccelerationStructureFeatures.accelerationStructureIndirectBuild
                    && khrAccelerationStructureFeatures.descriptorBindingAccelerationStructureUpdateAfterBind
                    //&& khrAccelerationStructureFeatures.accelerationStructureHostCommands
                && hasSupportForDeviceExtension(VK_KHR_RAY_QUERY_EXTENSION_NAME)
                    && khrRayQueryFeatures.rayQuery
                && hasSupportForDeviceExtension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME)
                && vk12features.bufferDeviceAddress;

            // We now only support the KHR ray tracing extension as it's the more generic/agnostic implementation
            if (nvidiaRayTracingSupport && !khrRayTracingSupport) {
                ARKOSE_LOG(Warning, "The VK_NV_ray_tracing extension is supported but the modern KHR-variants are not. "
                                    "Try updating your graphics drivers (it probably is supported on the latest drivers).");
            }

            bool opacityMicromapSupport = hasSupportForDeviceExtension(VK_EXT_OPACITY_MICROMAP_EXTENSION_NAME)
                && opacityMicromapFeatures.micromap;

            if (khrRayTracingSupport && !opacityMicromapSupport) {
                ARKOSE_LOG(Info, "VulkanBackend: ray tracing is supported but opacity micromaps are not. "
                                 "Support is not required, but it will improve performance if available.");
            }

            return khrRayTracingSupport;
        }
        case Capability::MeshShading: {
            bool supportsExtExtension = hasSupportForDeviceExtension(VK_EXT_MESH_SHADER_EXTENSION_NAME);
            bool extMeshShaderSupport = supportsExtExtension && meshShaderFeatures.taskShader && meshShaderFeatures.meshShader;

            // NOTE: For optimal data packing we really need to ensure we can pack indices with 8-bit integers, so we will require
            // this feature to be available if you use mesh shading (in practice, it will almost certainly be if mesh shading is).
            bool supportsShaderUint8 = vk12features.shaderInt8;

            if (!supportsExtExtension && hasSupportForDeviceExtension(VK_NV_MESH_SHADER_EXTENSION_NAME)) {
                ARKOSE_LOG(Error, "VulkanBackend: no support for mesh shading, but the Nvidia-specific extension is supported!"
                                  "If you update your drivers now it's possible that it will then be supported.");
            }

            return extMeshShaderSupport && supportsShaderUint8;
        }
        case Capability::Shader16BitFloat:
            return vk11features.storageBuffer16BitAccess
                && vk11features.uniformAndStorageBuffer16BitAccess
                && vk11features.storageInputOutput16
                && vk11features.storagePushConstant16
                && vk12features.shaderFloat16;

        case Capability::ShaderBarycentrics: {
            bool supportsExtension = hasSupportForDeviceExtension(VK_KHR_FRAGMENT_SHADER_BARYCENTRIC_EXTENSION_NAME);
            return supportsExtension && fragmentShaderBarycentricFeatures.fragmentShaderBarycentric;
        }
        default:
            ASSERT_NOT_REACHED();
        }
    };

    bool allRequiredSupported = true;

    if (!features.wideLines) {
        ARKOSE_LOG(Warning, "VulkanBackend: no support for wide lines feature. Lines may appear thin.");
    }

    if (!features.samplerAnisotropy || !features.fillModeNonSolid || !features.fragmentStoresAndAtomics || !features.vertexPipelineStoresAndAtomics) {
        ARKOSE_LOG(Error, "VulkanBackend: no support for required common device feature");
        allRequiredSupported = false;
    }

    if (!features.geometryShader) {
        ARKOSE_LOG(Error, "VulkanBackend: no support for geometry shaders, which while we don't use directly seems to be required for reading `gl_PrimitiveID` "
                          "in a fragment shader, which we do use. This requirement can possibly be removed if there's another way to do achieve the same result.");
        allRequiredSupported = false;
    }

    if (!vk11features.shaderDrawParameters) {
        ARKOSE_LOG(Error, "VulkanBackend: no support for required feature shader draw parameters, which is required for 'gl_DrawID' among others.");
        allRequiredSupported = false;
    }

    if (!features.shaderUniformBufferArrayDynamicIndexing || !vk12features.shaderUniformBufferArrayNonUniformIndexing ||
        !features.shaderStorageBufferArrayDynamicIndexing || !vk12features.shaderStorageBufferArrayNonUniformIndexing ||
        !features.shaderStorageImageArrayDynamicIndexing || !vk12features.shaderStorageImageArrayNonUniformIndexing ||
        !features.shaderSampledImageArrayDynamicIndexing || !vk12features.shaderSampledImageArrayNonUniformIndexing ||
        !vk12features.runtimeDescriptorArray || !vk12features.descriptorBindingVariableDescriptorCount) {
        ARKOSE_LOG(Error, "VulkanBackend: no support for required common dynamic & non-uniform indexing device features");
        allRequiredSupported = false;
    }

    if (!vk12features.runtimeDescriptorArray ||
        !vk12features.descriptorBindingVariableDescriptorCount ||
        !vk12features.descriptorBindingUpdateUnusedWhilePending ||
        !vk12features.descriptorBindingSampledImageUpdateAfterBind) {
        ARKOSE_LOG(Error, "VulkanBackend: no support for required common descriptor-binding device features");
        allRequiredSupported = false;
    }

    if (!vk12features.scalarBlockLayout) {
        ARKOSE_LOG(Error, "VulkanBackend: no support for scalar layout in shader storage blocks");
        allRequiredSupported = false;
    }

    if (!vk12features.drawIndirectCount) {
        ARKOSE_LOG(Error, "VulkanBackend: no support for required common drawing related device features");
        allRequiredSupported = false;
    }

    if (!vk12features.imagelessFramebuffer) {
        ARKOSE_LOG(Error, "VulkanBackend: no support for imageless framebuffers which is required");
        allRequiredSupported = false;
    }

    if (!features.textureCompressionBC) {
        ARKOSE_LOG(Error, "VulkanBackend: no support for BC compressed textures which is required");
        allRequiredSupported = false;
    }

    // NOTE: Not currently in use
    /*
    if (!features.shaderInt64 ||
        !vk12features.shaderBufferInt64Atomics ||
        !vk12features.shaderSharedInt64Atomics) {
        ARKOSE_LOG(Error, "VulkanBackend: no support for shader 64-bit atomics, which is required for our GPU work queue implementation. "
                          "If this isn't supported on your machine there might possibly be a version which doesn't require that.");
        allRequiredSupported = false;
    }
    */

    if (!vk13features.synchronization2) {
        ARKOSE_LOG(Error, "VulkanBackend: no support for 'synchronization2' which is required");
        allRequiredSupported = false;
    }

    if (!vk13features.maintenance4) {
        ARKOSE_LOG(Error, "VulkanBackend: no support for 'maintenance4', which is required for for various maintenance features.");
        allRequiredSupported = false;
    }

    if constexpr (vulkanDebugMode) {
        if (!(vk12features.bufferDeviceAddress && vk12features.bufferDeviceAddressCaptureReplay)) {
            ARKOSE_LOG(Error, "VulkanBackend: no support for buffer device address & buffer device address capture replay, which is required by e.g. Nsight for debugging. "
                              "If this is a problem, try compiling and running with vulkanDebugMode set to false.");
            allRequiredSupported = false;
        }
    }

    for (auto& cap : appSpecification.requiredCapabilities) {
        if (isSupported(cap)) {
            m_activeCapabilities[cap] = true;
        } else {
            ARKOSE_LOG(Error, "VulkanBackend: no support for required '{}' capability", capabilityName(cap));
            allRequiredSupported = false;
        }
    }

    for (auto& cap : appSpecification.optionalCapabilities) {
        if (isSupported(cap)) {
            m_activeCapabilities[cap] = true;
        } else {
            ARKOSE_LOG(Info, "VulkanBackend: no support for optional '{}' capability", capabilityName(cap));
        }
    }

    return allRequiredSupported;
}

std::unique_ptr<Buffer> VulkanBackend::createBuffer(size_t size, Buffer::Usage usage)
{
    return std::make_unique<VulkanBuffer>(*this, size, usage);
}

std::unique_ptr<RenderTarget> VulkanBackend::createRenderTarget(std::vector<RenderTarget::Attachment> attachments)
{
    return std::make_unique<VulkanRenderTarget>(*this, attachments);
}

std::unique_ptr<Sampler> VulkanBackend::createSampler(Sampler::Description desc)
{
    return std::make_unique<VulkanSampler>(*this, desc);
}

std::unique_ptr<Texture> VulkanBackend::createTexture(Texture::Description desc)
{
    return std::make_unique<VulkanTexture>(*this, desc);
}

std::unique_ptr<BindingSet> VulkanBackend::createBindingSet(std::vector<ShaderBinding> shaderBindings)
{
    return std::make_unique<VulkanBindingSet>(*this, shaderBindings);
}

std::unique_ptr<RenderState> VulkanBackend::createRenderState(const RenderTarget& renderTarget, const std::vector<VertexLayout>& vertexLayouts,
                                                              const Shader& shader, const StateBindings& stateBindings,
                                                              const RasterState& rasterState, const DepthState& depthState, const StencilState& stencilState)
{
    return std::make_unique<VulkanRenderState>(*this, renderTarget, vertexLayouts, shader, stateBindings, rasterState, depthState, stencilState);
}

std::unique_ptr<BottomLevelAS> VulkanBackend::createBottomLevelAccelerationStructure(std::vector<RTGeometry> geometries)
{
    ARKOSE_ASSERT(hasRayTracingSupport());
    return std::make_unique<VulkanBottomLevelASKHR>(*this, geometries);
}

std::unique_ptr<TopLevelAS> VulkanBackend::createTopLevelAccelerationStructure(uint32_t maxInstanceCount)
{
    ARKOSE_ASSERT(hasRayTracingSupport());
    return std::make_unique<VulkanTopLevelASKHR>(*this, maxInstanceCount);
}

std::unique_ptr<RayTracingState> VulkanBackend::createRayTracingState(ShaderBindingTable& sbt, const StateBindings& stateBindings, uint32_t maxRecursionDepth)
{
    ARKOSE_ASSERT(hasRayTracingSupport());
    return std::make_unique<VulkanRayTracingStateKHR>(*this, sbt, stateBindings, maxRecursionDepth);
}

std::unique_ptr<ComputeState> VulkanBackend::createComputeState(Shader const& shader, StateBindings const& stateBindings)
{
    return std::make_unique<VulkanComputeState>(*this, shader, stateBindings);
}

std::unique_ptr<ExternalFeature> VulkanBackend::createExternalFeature(ExternalFeatureType type, void* externalFeatureParameters)
{
    switch (type) {
    case ExternalFeatureType::None:
        ASSERT_NOT_REACHED();
    case ExternalFeatureType::DLSS: {
        #if WITH_DLSS
        if (m_dlss && m_dlss->isReadyToUse()) {
            auto const& dlssParams = *static_cast<ExternalFeatureCreateParamsDLSS const*>(externalFeatureParameters);
            return std::make_unique<VulkanDLSSExternalFeature>(*this, dlssParams);
        } else
        #endif
        {
            ARKOSE_LOG(Error, "VulkanBackend: cannot create DLSS external feature, not supported!");
            return nullptr;
        }
    }
    case ExternalFeatureType::NRD_SigmaShadow: {
        #if WITH_NRD
        if (m_nrd && m_nrd->isReadyToUse()) {
            auto const& nrdSigmaShadowParams = *static_cast<ExternalFeatureCreateParamsNRDSigmaShadow const*>(externalFeatureParameters);
            return std::make_unique<VulkanNRDSigmaShadowExternalFeature>(*this, *m_nrd, nrdSigmaShadowParams);
        } else
        #endif
        {
            ARKOSE_LOG(Error, "VulkanBackend: cannot create NRD_SigmaShadow external feature, not supported!");
            return nullptr;
        }
    }
    default:
        ARKOSE_LOG(Error, "VulkanBackend: cannot create external feature of unknown type {}", type);
        return nullptr;
    }
}

VkSurfaceFormatKHR VulkanBackend::pickBestSurfaceFormat() const
{
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> surfaceFormats { formatCount };
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, surfaceFormats.data());

    VkSurfaceFormatKHR const* optimalSdrSrgbFormat = nullptr;
    VkSurfaceFormatKHR const* optimalHdrHdr10Format = nullptr;

    for (const auto& format : surfaceFormats) {
        // Note that we use the *_UNORM format here and thus require some pass to convert colors to sRGB-encoded before final output.
        // Another option is to use e.g. VK_FORMAT_B8G8R8A8_SRGB and then let the drivers convert to sRGB-encoded automatically.
        // See this stackoverflow answer for more information: https://stackoverflow.com/a/66401423.
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            optimalSdrSrgbFormat = &format;
        } else if (format.format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 && format.colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT) {
            optimalHdrHdr10Format = &format;
        }
    }

    if (optimalHdrHdr10Format) {
        ARKOSE_LOG(Info, "VulkanBackend: using 10-bit HDR10 (ST2084/PQ) surface format.");
        return *optimalHdrHdr10Format;
    }

    if (optimalSdrSrgbFormat) {
        ARKOSE_LOG(Info, "VulkanBackend: using 8-bit sRGB surface format.");
        return *optimalSdrSrgbFormat;
    }

    // If we didn't find the optimal one, just chose an arbitrary one
    ARKOSE_LOG(Info, "VulkanBackend: couldn't find preferred surface format, so picked arbitrary supported format.");
    VkSurfaceFormatKHR format = surfaceFormats[0];

    if (format.colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        ARKOSE_LOG(Warning, "VulkanBackend: could not find a sRGB surface format, so images won't be pretty!");
    }

    return format;
}

VkPresentModeKHR VulkanBackend::pickBestPresentMode() const
{
    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes { presentModeCount };
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, presentModes.data());

    for (const auto& mode : presentModes) {
        // Try to chose the mailbox mode, i.e. use-last-fully-generated-image mode
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            ARKOSE_LOG(Info, "VulkanBackend: using mailbox present mode.");
            return mode;
        }
    }

    // VK_PRESENT_MODE_FIFO_KHR is guaranteed to be available and it basically corresponds to normal v-sync so it's fine
    ARKOSE_LOG(Info, "VulkanBackend: using v-sync present mode.");
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanBackend::pickBestSwapchainExtent() const
{
    VkSurfaceCapabilitiesKHR surfaceCapabilities {};

    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &surfaceCapabilities) != VK_SUCCESS) {
        ARKOSE_LOG(Fatal, "VulkanBackend: could not get surface capabilities, exiting.");
    }

    if (surfaceCapabilities.currentExtent.width != UINT32_MAX) {
        // The surface has specified the extent (probably to whatever the window extent is) and we should choose that
        ARKOSE_LOG(Info, "VulkanBackend: using optimal window extents for swap chain.");
        return surfaceCapabilities.currentExtent;
    }

    // The drivers are flexible, so let's choose something good that is within the the legal extents
    VkExtent2D extent = {};

    Extent2D framebufferSize = System::get().windowFramebufferSize();

    extent.width = std::clamp(static_cast<uint32_t>(framebufferSize.width()), surfaceCapabilities.minImageExtent.width, surfaceCapabilities.maxImageExtent.width);
    extent.height = std::clamp(static_cast<uint32_t>(framebufferSize.height()), surfaceCapabilities.minImageExtent.height, surfaceCapabilities.maxImageExtent.height);
    ARKOSE_LOG(Info, "VulkanBackend: using specified extents ({} x {}) for swap chain.", extent.width, extent.height);

    return extent;
}

VkInstance VulkanBackend::createInstance(const std::vector<const char*>& requestedLayers, VkDebugUtilsMessengerCreateInfoEXT* debugMessengerCreateInfo)
{
    SCOPED_PROFILE_ZONE_BACKEND();

    for (auto& layer : requestedLayers) {
        if (!hasSupportForLayer(layer))
            ARKOSE_LOG(Fatal, "VulkanBackend: missing layer '{}'", layer);
    }

    bool includeValidationFeatures = false;
    std::vector<const char*> instanceExtensions;
    auto addInstanceExtension = [&](const char* extension) {
        if (m_enabledInstanceExtensions.find(extension) == m_enabledInstanceExtensions.end()) {
            instanceExtensions.emplace_back(extension);
            m_enabledInstanceExtensions.insert(extension);
        }
    };

    {
        uint32_t requiredCount;
        char const** requiredExtensions = System::get().requiredInstanceExtensions(&requiredCount);
        for (uint32_t i = 0; i < requiredCount; ++i) {
            const char* name = requiredExtensions[i];
            ARKOSE_ASSERT(hasSupportForInstanceExtension(name));
            addInstanceExtension(name);
        }

        #if __APPLE__
        {
            // Required when running Vulkan in portability mode, e.g., through MoltenVK on macOS
            ARKOSE_ASSERT(hasSupportForInstanceExtension(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME));
            addInstanceExtension(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        }
        #endif

        // Required for checking support of complex features. It's probably fine to always require it. If it doesn't exist, we deal with it then..
        ARKOSE_ASSERT(hasSupportForInstanceExtension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME));
        addInstanceExtension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

        // For debug messages etc.
        if constexpr (vulkanDebugMode) {
            ARKOSE_ASSERT(hasSupportForInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME));
            addInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

            if (hasSupportForInstanceExtension(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME)) {
                addInstanceExtension(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
                includeValidationFeatures = true;
            }
        }

        #if WITH_DLSS
        {
            for (VkExtensionProperties const* extension : VulkanDLSS::requiredInstanceExtensions()) {
                if (hasSupportForInstanceExtension(extension->extensionName)) {
                    addInstanceExtension(extension->extensionName);
                } else {
                    m_dlssHasAllRequiredExtensions = false;
                }
            }
        }
        #endif
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
    appInfo.apiVersion = VulkanApiVersion; // NOLINT(hicpp-signed-bitwise)

    VkInstanceCreateInfo instanceCreateInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    instanceCreateInfo.pApplicationInfo = &appInfo;

    instanceCreateInfo.enabledExtensionCount = (uint32_t)instanceExtensions.size();
    instanceCreateInfo.ppEnabledExtensionNames = instanceExtensions.data();

    instanceCreateInfo.enabledLayerCount = (uint32_t)requestedLayers.size();
    instanceCreateInfo.ppEnabledLayerNames = requestedLayers.data();

    instanceCreateInfo.flags = 0;
    #if __APPLE__
    {
        instanceCreateInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
    #endif

    if (debugMessengerCreateInfo) {
        instanceCreateInfo.pNext = debugMessengerCreateInfo;
        if (includeValidationFeatures) {
            debugMessengerCreateInfo->pNext = &validationFeatures;
        }
    }

    VkInstance instance;
    if (vkCreateInstance(&instanceCreateInfo, nullptr, &instance) != VK_SUCCESS)
        ARKOSE_LOG(Fatal, "VulkanBackend: could not create instance.");

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
    auto addDeviceExtension = [&](const char* extension) {
        if (m_enabledDeviceExtensions.find(extension) == m_enabledDeviceExtensions.end()) {
            deviceExtensions.emplace_back(extension);
            m_enabledDeviceExtensions.insert(extension);
        }
    };

    ARKOSE_ASSERT(hasSupportForDeviceExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME));
    addDeviceExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    // Used to query for VRAM memory usage (also automatically used by VulkanMemoryAllocator internally)
    if (hasSupportForDeviceExtension(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME))
        addDeviceExtension(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);

    // Automatically used by VulkanMemoryAllocator internally to create dedicated allocations.
    // See this blog post for more info: https://www.asawicki.info/articles/VK_KHR_dedicated_allocation.php5
    if (hasSupportForDeviceExtension(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME))
        addDeviceExtension(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME);

    if (hasSupportForDeviceExtension(VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME))
        addDeviceExtension(VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME);

    #if defined(TRACY_ENABLE)
        ARKOSE_ASSERT(hasSupportForDeviceExtension(VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME));
        addDeviceExtension(VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME);
    #endif

    #if WITH_DLSS
    {
        if (m_dlssHasAllRequiredExtensions && !m_renderdocAPI) {
            for (VkExtensionProperties const* extension : VulkanDLSS::requiredDeviceExtensions(m_instance, physicalDevice)) {
                if (hasSupportForDeviceExtension(extension->extensionName)) {
                    addDeviceExtension(extension->extensionName);
                } else {
                    m_dlssHasAllRequiredExtensions = false;
                }
            }
        }
    }
    #endif

    VkPhysicalDeviceFeatures2 features2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    VkPhysicalDeviceFeatures& vk10features = features2.features;
    VkPhysicalDeviceVulkan11Features vk11features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
    VkPhysicalDeviceVulkan12Features vk12features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    VkPhysicalDeviceVulkan13Features vk13features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    VkPhysicalDeviceVulkan14Features vk14features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES };

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR khrRayTracingPipelineFeatures { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR };
    VkPhysicalDeviceAccelerationStructureFeaturesKHR khrAccelerationStructureFeatures { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
    VkPhysicalDeviceRayQueryFeaturesKHR khrRayQueryFeatures { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR };
    VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT };
    VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR fragmentShaderBarycentricFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_BARYCENTRIC_FEATURES_KHR };

    VkPhysicalDeviceDiagnosticsConfigFeaturesNV nvDeviceDiagnosticsFeatues = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DIAGNOSTICS_CONFIG_FEATURES_NV };
    VkDeviceDiagnosticsConfigCreateInfoNV nvDeviceDiagnosticsCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_DIAGNOSTICS_CONFIG_CREATE_INFO_NV };

    // Enable some very basic common features expected by everyone to exist
    vk10features.samplerAnisotropy = VK_TRUE;
    vk10features.fillModeNonSolid = VK_TRUE;
    vk10features.wideLines = VK_TRUE;
    vk10features.fragmentStoresAndAtomics = VK_TRUE;
    vk10features.vertexPipelineStoresAndAtomics = VK_TRUE;

    // NOTE: We only use this to read `gl_PrimitiveID` in the fragment shader. See this for context:
    // https://computergraphics.stackexchange.com/questions/9449/vulkan-using-gl-primitiveid-without-geometryshader-feature
    vk10features.geometryShader = VK_TRUE;

    // Common shader parameters, such as 'gl_DrawID'
    vk11features.shaderDrawParameters = VK_TRUE;
    
    // Common dynamic & non-uniform indexing features that should be supported on a modern GPU
    vk10features.shaderUniformBufferArrayDynamicIndexing = VK_TRUE;
    vk12features.shaderUniformBufferArrayNonUniformIndexing = VK_TRUE;
    vk10features.shaderStorageBufferArrayDynamicIndexing = VK_TRUE;
    vk12features.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
    vk10features.shaderStorageImageArrayDynamicIndexing = VK_TRUE;
    vk12features.shaderStorageImageArrayNonUniformIndexing = VK_TRUE;
    vk10features.shaderSampledImageArrayDynamicIndexing = VK_TRUE;
    vk12features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;

    // Common descriptor binding features that should be supported on a modern GPU
    vk12features.runtimeDescriptorArray = VK_TRUE;
    vk12features.descriptorBindingVariableDescriptorCount = VK_TRUE;
    vk12features.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
    vk12features.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;

    // Common drawing related features
    vk12features.drawIndirectCount = VK_TRUE;

    // Scalar block layout in shaders
    vk12features.scalarBlockLayout = VK_TRUE;

    // Imageless framebuffers
    vk12features.imagelessFramebuffer = VK_TRUE;

    // BC texture compression
    vk10features.textureCompressionBC = VK_TRUE;

    // 64-bit shader support including atomics
    //features.shaderInt64 = VK_TRUE;
    //vk12features.shaderBufferInt64Atomics = VK_TRUE;
    //vk12features.shaderSharedInt64Atomics = VK_TRUE;

    // The way we now transition the swapchain image layout apparently requires this..
    vk13features.synchronization2 = VK_TRUE;

    // 'maintenance4' for various maintenance features
    vk13features.maintenance4 = VK_TRUE;

    // GPU debugging & insight for e.g. Nsight
    if constexpr (vulkanDebugMode) {
        vk12features.bufferDeviceAddress = VK_TRUE;
        vk12features.bufferDeviceAddressCaptureReplay = VK_TRUE;
    }

    void* nextChain = nullptr;
    auto appendToNextChain = [&](auto& object) -> void {
        object.pNext = nextChain;
        nextChain = &object;
    };

    appendToNextChain(features2); // for `vk10features`
    appendToNextChain(vk11features);
    appendToNextChain(vk12features);
    appendToNextChain(vk13features);
    appendToNextChain(vk14features);

    for (auto& [capability, active] : m_activeCapabilities) {
        if (!active)
            continue;
        switch (capability) {
        case Capability::RayTracing:
            deviceExtensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
            appendToNextChain(khrRayTracingPipelineFeatures);
            khrRayTracingPipelineFeatures.rayTracingPipeline = VK_TRUE;
            khrRayTracingPipelineFeatures.rayTracingPipelineTraceRaysIndirect = VK_TRUE;
            khrRayTracingPipelineFeatures.rayTraversalPrimitiveCulling = VK_TRUE;
            deviceExtensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
            appendToNextChain(khrAccelerationStructureFeatures);
            khrAccelerationStructureFeatures.accelerationStructure = VK_TRUE;
            //khrAccelerationStructureFeatures.accelerationStructureIndirectBuild = VK_TRUE;
            khrAccelerationStructureFeatures.descriptorBindingAccelerationStructureUpdateAfterBind = VK_TRUE;
            //khrAccelerationStructureFeatures.accelerationStructureHostCommands = VK_TRUE;
            deviceExtensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
            appendToNextChain(khrRayQueryFeatures);
            khrRayQueryFeatures.rayQuery = VK_TRUE;
            deviceExtensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
            vk12features.bufferDeviceAddress = VK_TRUE;
            break;
        case Capability::MeshShading:
            deviceExtensions.push_back(VK_EXT_MESH_SHADER_EXTENSION_NAME);
            appendToNextChain(meshShaderFeatures);
            meshShaderFeatures.taskShader = VK_TRUE;
            meshShaderFeatures.meshShader = VK_TRUE;
            vk12features.shaderInt8 = VK_TRUE;
            break;
        case Capability::Shader16BitFloat:
            vk11features.storageBuffer16BitAccess = VK_TRUE;
            vk11features.uniformAndStorageBuffer16BitAccess = VK_TRUE;
            vk11features.storageInputOutput16 = VK_TRUE;
            vk11features.storagePushConstant16 = VK_TRUE;
            vk12features.shaderFloat16 = VK_TRUE;
            break;
        case Capability::ShaderBarycentrics:
            deviceExtensions.push_back(VK_KHR_FRAGMENT_SHADER_BARYCENTRIC_EXTENSION_NAME);
            appendToNextChain(fragmentShaderBarycentricFeatures);
            fragmentShaderBarycentricFeatures.fragmentShaderBarycentric = VK_TRUE;
            break;
        default:
            ASSERT_NOT_REACHED();
        }
    }

    #if WITH_AFTERMATH_SDK
    if (vulkanDebugMode && !m_renderdocAPI) {
        if (hasSupportForDeviceExtension(VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME)
            && hasSupportForDeviceExtension(VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME)) {

            addDeviceExtension(VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME);
            addDeviceExtension(VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME);

            appendToNextChain(nvDeviceDiagnosticsFeatues);
            nvDeviceDiagnosticsFeatues.diagnosticsConfig = VK_TRUE;

            appendToNextChain(nvDeviceDiagnosticsCreateInfo);
            nvDeviceDiagnosticsCreateInfo.flags |= VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_SHADER_DEBUG_INFO_BIT_NV;
            nvDeviceDiagnosticsCreateInfo.flags |= VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_RESOURCE_TRACKING_BIT_NV;
            nvDeviceDiagnosticsCreateInfo.flags |= VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_AUTOMATIC_CHECKPOINTS_BIT_NV;
            nvDeviceDiagnosticsCreateInfo.flags |= VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_SHADER_ERROR_REPORTING_BIT_NV;

            GFSDK_Aftermath_Result aftermathEnableRes = GFSDK_Aftermath_EnableGpuCrashDumps(
                GFSDK_Aftermath_Version_API,
                GFSDK_Aftermath_GpuCrashDumpWatchedApiFlags_Vulkan,
                GFSDK_Aftermath_GpuCrashDumpFeatureFlags_Default,
                AftermathGpuCrashCallback,
                AftermathGpuCrashShaderInfoCallback,
                nullptr,
                nullptr,
                nullptr);

            if (aftermathEnableRes == GFSDK_Aftermath_Result_Success) {
                ARKOSE_LOG(Info, "VulkanBackend: NVIDIA Nsight Aftermath armed & waiting");
                AftermathCrashDumpCollectionActive = true;
            }
        }
    }
    #endif

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

    deviceCreateInfo.pNext = nextChain;

    VkDevice device;
    if (vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device) != VK_SUCCESS)
        ARKOSE_LOG(Fatal, "VulkanBackend: could not create a device, exiting.");

    return device;
}

bool VulkanBackend::checkForResizableBARSupport() const
{
    // Find the largest heap of device-local memory
    u32 largestDeviceLocalMemoryHeapIndex = 0;
    VkMemoryHeap largestDeviceLocalHeap = { .size = 0 };
    for (u32 heapIdx = 0; heapIdx < m_physicalDeviceMemoryProperties.memoryHeapCount; ++heapIdx) {
        if (m_physicalDeviceMemoryProperties.memoryHeaps[heapIdx].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            VkMemoryHeap const& heap = m_physicalDeviceMemoryProperties.memoryHeaps[heapIdx];
            if (heap.size > largestDeviceLocalHeap.size) {
                largestDeviceLocalHeap = heap;
                largestDeviceLocalMemoryHeapIndex = heapIdx;
            }
        }
    }

    // See if we can find a memory type which is both device-local and host-visible, and which belongs to the largest device-local heap
    // If so, that indicates we support Resizable BAR (Re-BAR) and can use it to avoid staging buffers where applicable.
    for (u32 typeIdx = 0; typeIdx < m_physicalDeviceMemoryProperties.memoryTypeCount; ++typeIdx) {
        VkMemoryType memoryType = m_physicalDeviceMemoryProperties.memoryTypes[typeIdx];
        if ((memoryType.propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) && (memoryType.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
            if (memoryType.heapIndex == largestDeviceLocalMemoryHeapIndex) {
                return true;
            }
        }
    }

    return false;
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
        ARKOSE_LOG(Fatal, "VulkanBackend: could not find a graphics queue, exiting.");
    }
    if (!foundComputeQueue) {
        ARKOSE_LOG(Fatal, "VulkanBackend: could not find a compute queue, exiting.");
    }
    if (!foundPresentQueue) {
        ARKOSE_LOG(Fatal, "VulkanBackend: could not find a present queue, exiting.");
    }
}

VkPhysicalDevice VulkanBackend::pickBestPhysicalDevice() const
{
    SCOPED_PROFILE_ZONE_BACKEND();

    uint32_t count;
    vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
    if (count < 1) {
        ARKOSE_LOG(Fatal, "VulkanBackend: could not find any physical devices with Vulkan support, exiting.");
    }

    std::vector<VkPhysicalDevice> physicalDevices(count);
    vkEnumeratePhysicalDevices(m_instance, &count, physicalDevices.data());

    if (count == 1) {
        return physicalDevices[0];
    }

    std::vector<VkPhysicalDevice> discretePhysicalDevices;
    std::vector<VkPhysicalDevice> otherPhysicalDevices;

    for (VkPhysicalDevice physicalDevice : physicalDevices) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(physicalDevice, &properties);

        if ( properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ) {
            discretePhysicalDevices.push_back(physicalDevice);
        } else {
            otherPhysicalDevices.push_back(physicalDevice);
        }
    }

    if (discretePhysicalDevices.size() >= 1) {
        if (discretePhysicalDevices.size() > 1) {
            ARKOSE_LOG(Warning, "VulkanBackend: more than one discrete physical device with Vulkan support, picking one arbitrarily.");
        }
        return discretePhysicalDevices[0];
    }

    ARKOSE_LOG(Warning, "VulkanBackend: could not find any discrete physical devices with Vulkan support, picking an arbitrary one.");
    ARKOSE_ASSERT(otherPhysicalDevices.size() > 0);
    return otherPhysicalDevices[0];
}

VkPipelineCache VulkanBackend::createAndLoadPipelineCacheFromDisk() const
{
    SCOPED_PROFILE_ZONE_BACKEND();

    VkPipelineCacheCreateInfo pipelineCacheInfo = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };

    // TODO: Maybe do some validation on the data e.g. in case version change? On the other hand, it's easy to just delete the cache if it doesn't load properly..
    auto maybeCacheData = FileIO::readBinaryDataFromFile<char>(pipelineCacheFilePath);
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
        ARKOSE_LOG(Fatal, "VulkanBackend: could not create pipeline cache, exiting.");
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

    FileIO::writeBinaryDataToFile(pipelineCacheFilePath, data);
}

void VulkanBackend::createSwapchain(VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface)
{
    SCOPED_PROFILE_ZONE_BACKEND();

    VkSurfaceCapabilitiesKHR surfaceCapabilities;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCapabilities) != VK_SUCCESS) {
        ARKOSE_LOG(Fatal, "VulkanBackend: could not get surface capabilities, exiting.");
    }

    VkSwapchainCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    createInfo.surface = surface;

    // See https://github.com/KhronosGroup/Vulkan-Docs/issues/909 for discussion regarding +1
    createInfo.minImageCount = surfaceCapabilities.minImageCount + 1;
    if (surfaceCapabilities.maxImageCount != 0) {
        createInfo.minImageCount = std::min(createInfo.minImageCount, surfaceCapabilities.maxImageCount);
    }

    m_surfaceFormat = pickBestSurfaceFormat();
    createInfo.imageFormat = m_surfaceFormat.format;
    createInfo.imageColorSpace = m_surfaceFormat.colorSpace;

    VkPresentModeKHR presentMode = pickBestPresentMode();
    createInfo.presentMode = presentMode;

    VkExtent2D swapchainExtent = pickBestSwapchainExtent();
    m_swapchainExtent = { swapchainExtent.width, swapchainExtent.height };
    createInfo.imageExtent = swapchainExtent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT; // TODO: What do we want here? Maybe this suffices?
    // TODO: Assure VK_IMAGE_USAGE_STORAGE_BIT is supported using vkGetPhysicalDeviceSurfaceCapabilitiesKHR & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT

    if constexpr (vulkanDebugMode) {
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

    if (surfaceCapabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) {
        createInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    } else {
        ARKOSE_LOG(Warning, "VulkanBackend: surface does not support identity transform, using current transform instead, which may not be entirely correct.");
        createInfo.preTransform = surfaceCapabilities.currentTransform;
    }

    if (surfaceCapabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) {
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    } else {
        ARKOSE_LOG(Warning, "VulkanBackend: surface does not support opaque composite alpha, using some other composite alpha mode instead.");
        if (surfaceCapabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) {
            createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
        } else if (surfaceCapabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) {
            createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
        } else if (surfaceCapabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR) {
            createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
        }
    }

    createInfo.clipped = VK_TRUE; // clip pixels obscured by other windows etc.

    createInfo.oldSwapchain = m_swapchain;
    m_swapchain = nullptr;

    if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &m_swapchain) != VK_SUCCESS) {
        ARKOSE_LOG(Fatal, "VulkanBackend: could not create swapchain, exiting.");
    }

    // Destroy old swapchain & associated data

    if (createInfo.oldSwapchain) {
        for (auto& swapchainImageContext : m_swapchainImageContexts) {
            vkDestroySemaphore(device, swapchainImageContext->submitSemaphore, nullptr);
            vkDestroyImageView(device, swapchainImageContext->imageView, nullptr);
        }
        m_swapchainImageContexts.clear();

        vkDestroySwapchainKHR(device, createInfo.oldSwapchain, nullptr);
    }

    // Create associated data

    ARKOSE_ASSERT(m_swapchainImageContexts.size() == 0);

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
            imageViewCreateInfo.format = m_surfaceFormat.format;

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
                ARKOSE_LOG(Fatal, "VulkanBackend: could not create image view {} (out of {}), exiting.", imageIdx, numSwapchainImages);
            }

            swapchainImageContext->imageView = swapchainImageView;
        }

        // Create submit semaphore
        {
            VkSemaphoreCreateInfo semaphoreCreateInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
            if (vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &swapchainImageContext->submitSemaphore) != VK_SUCCESS) {
                ARKOSE_LOG(Fatal, "VulkanBackend: could not create submitSemaphore, exiting.");
            }
        }

        m_swapchainImageContexts.push_back(std::move(swapchainImageContext));
    }

    // Create placeholder VulkanTexture as a stand-in for the swapchain image,
    // where the exact image + imageView is not known until the frame begins.
    m_placeholderSwapchainTexture = VulkanTexture::createSwapchainPlaceholderTexture(m_swapchainExtent, createInfo.imageUsage, m_surfaceFormat.format);

    if (m_guiIsSetup) {
        ImGui_ImplVulkan_SetMinImageCount(createInfo.minImageCount);
    }
}

void VulkanBackend::destroySwapchain()
{
    SCOPED_PROFILE_ZONE_BACKEND();

    for (auto& swapchainImageContext : m_swapchainImageContexts) {
        vkDestroySemaphore(device(), swapchainImageContext->submitSemaphore, nullptr);
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
        Extent2D framebufferExtent = System::get().windowFramebufferSize();
        if (framebufferExtent.hasZeroArea()) {
            ARKOSE_LOG(Info, "VulkanBackend: rendering paused since there are no pixels to draw to.");
            System::get().waitEvents();
        } else {
            ARKOSE_LOG(Info, "VulkanBackend: rendering resumed.");
            break;
        }
    }

    vkDeviceWaitIdle(device());
    createSwapchain(physicalDevice(), device(), m_surface);

    // Re-create the ImGui render target with the new placeholder texture
    auto imguiAttachments = std::vector<RenderTarget::Attachment>({ { RenderTarget::AttachmentType::Color0, m_placeholderSwapchainTexture.get(), LoadOp::Load, StoreOp::Store } });
    m_imguiRenderTarget = std::make_unique<VulkanRenderTarget>(*this, imguiAttachments);

    m_relativeFrameIndex = 0;

    return m_swapchainExtent;
}

void VulkanBackend::createFrameContexts()
{
    for (int i = 0; i < NumInFlightFrames; ++i) {

        if (m_frameContexts[i] == nullptr)
            m_frameContexts[i] = std::make_unique<FrameContext>();
        FrameContext& frameContext = *m_frameContexts[i];

        // Create upload buffer
        {
            static constexpr size_t registryUploadBufferSize = 100 * 1024 * 1024; // TODO: Make less ridiculously big!
            frameContext.uploadBuffer = std::make_unique<UploadBuffer>(*this, registryUploadBufferSize);
        }

        // Create fence
        {
            VkFenceCreateInfo fenceCreateInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
            fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

            if (vkCreateFence(device(), &fenceCreateInfo, nullptr, &frameContext.frameFence) != VK_SUCCESS) {
                ARKOSE_LOG(Fatal, "VulkanBackend: could not create frame context fence, exiting.");
            }
        }

        // Create "image available" semaphore
        {
            VkSemaphoreCreateInfo semaphoreCreateInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

            if (vkCreateSemaphore(device(), &semaphoreCreateInfo, nullptr, &frameContext.imageAvailableSemaphore) != VK_SUCCESS) {
                ARKOSE_LOG(Fatal, "VulkanBackend: could not create imageAvailableSemaphore, exiting.");
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
                ARKOSE_LOG(Fatal, "VulkanBackend: could not create command buffer, exiting.");
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
                ARKOSE_LOG(Fatal, "VulkanBackend: could not create timestamp query pool, exiting.");
            }

            frameContext.timestampQueryPool = timestampQueryPool;
        }
    }
}

void VulkanBackend::destroyFrameContexts()
{
    for (std::unique_ptr<FrameContext>& frameContext : m_frameContexts) {
        vkDestroyQueryPool(device(), frameContext->timestampQueryPool, nullptr);
        vkFreeCommandBuffers(device(), m_defaultCommandPool, 1, &frameContext->commandBuffer);
        vkDestroySemaphore(device(), frameContext->imageAvailableSemaphore, nullptr);
        vkDestroyFence(device(), frameContext->frameFence, nullptr);
        frameContext.reset();
    }
}

void VulkanBackend::setupDearImgui()
{
    SCOPED_PROFILE_ZONE_BACKEND();

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
        ARKOSE_LOG(Fatal, "DearImGui error while setting up descriptor pool");
    }

    ImGui_ImplVulkan_InitInfo initInfo = {};
    initInfo.CheckVkResultFn = [](VkResult result) {
        if (result != VK_SUCCESS) {
            ARKOSE_LOG(Fatal, "DearImGui vulkan error!");
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

    auto imguiAttachments = std::vector<RenderTarget::Attachment>({ { RenderTarget::AttachmentType::Color0, m_placeholderSwapchainTexture.get(), LoadOp::Load, StoreOp::Store } });
    m_imguiRenderTarget = std::make_unique<VulkanRenderTarget>(*this, imguiAttachments);

    VkRenderPass compatibleRenderPassForImGui = m_imguiRenderTarget->compatibleRenderPass;
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
    m_imguiRenderTarget.reset();
    m_guiIsSetup = false;
}

void VulkanBackend::renderDearImguiFrame(VkCommandBuffer commandBuffer, FrameContext& frameContext, SwapchainImageContext& swapchainImageContext)
{
    // Transition all textures that will be used for ImGui rendering to the required image layout
    if (VulkanTexture::texturesForImGuiRendering.size() > 0) {
        std::vector<VkImageMemoryBarrier> imageMemoryBarriers {};
        for (VulkanTexture* texture : VulkanTexture::texturesForImGuiRendering) {
            ARKOSE_ASSERT(texture->currentLayout != VK_IMAGE_LAYOUT_UNDEFINED);
            if (texture->currentLayout != VulkanTexture::ImGuiRenderingTargetLayout) {

                VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                barrier.oldLayout = texture->currentLayout;
                barrier.newLayout = VulkanTexture::ImGuiRenderingTargetLayout;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

                barrier.image = texture->image;
                barrier.subresourceRange.aspectMask = texture->aspectMask();

                // Ensure all writing is done before it can be read in a shader (the ImGui shader)
                barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

                barrier.subresourceRange.baseArrayLayer = 0;
                barrier.subresourceRange.layerCount = texture->layerCount();
                barrier.subresourceRange.baseMipLevel = 0;
                barrier.subresourceRange.levelCount = texture->mipLevels();

                imageMemoryBarriers.push_back(barrier);
                texture->currentLayout = VulkanTexture::ImGuiRenderingTargetLayout;
            }
        }
        VulkanTexture::texturesForImGuiRendering.clear();

        if (imageMemoryBarriers.size() > 0) {
            vkCmdPipelineBarrier(commandBuffer,
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                                 0, nullptr,
                                 0, nullptr,
                                 narrow_cast<u32>(imageMemoryBarriers.size()), imageMemoryBarriers.data());
        }
    }

    VkRenderPassBeginInfo passBeginInfo = {};
    passBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    passBeginInfo.renderPass = m_imguiRenderTarget->compatibleRenderPass;
    passBeginInfo.framebuffer = m_imguiRenderTarget->framebuffer;
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
}

void VulkanBackend::waitForFrameReady()
{
    SCOPED_PROFILE_ZONE_BACKEND();

    uint32_t frameContextIndex = m_currentFrameIndex % m_frameContexts.size();
    FrameContext& frameContext = *m_frameContexts[frameContextIndex];

    // Wait indefinitely, or as long as the drivers will allow
    uint64_t timeout = UINT64_MAX;

    VkResult result = vkWaitForFences(device(), 1, &frameContext.frameFence, VK_TRUE, timeout);

    if (result == VK_ERROR_DEVICE_LOST) {

        #if defined(WITH_AFTERMATH_SDK)
        if (AftermathCrashDumpCollectionActive) {
            ARKOSE_LOG(Warning, "VulkanBackend: device was lost, waiting for Aftermath to collect data...");

            GFSDK_Aftermath_CrashDump_Status status = GFSDK_Aftermath_CrashDump_Status_Unknown;
            GFSDK_Aftermath_GetCrashDumpStatus(&status);

            while (status != GFSDK_Aftermath_CrashDump_Status_CollectingDataFailed && status != GFSDK_Aftermath_CrashDump_Status_Finished) {
                ARKOSE_LOG(Warning, "VulkanBackend: waiting for Aftermath...");

                using namespace std::chrono_literals;
                std::this_thread::sleep_for(50ms);

                GFSDK_Aftermath_GetCrashDumpStatus(&status);
            }

            if (status == GFSDK_Aftermath_CrashDump_Status_Finished) {
                ARKOSE_LOG(Warning, "VulkanBackend: Aftermath has written a GPU crash dump, exiting");
            }

            // TODO: See if we can recover!
            exit(1);
        } else
        #endif
        {
            ARKOSE_LOG(Fatal, "VulkanBackend: device was lost while waiting for frame fence (frame {}).", m_currentFrameIndex);
        }
    }

    if (vkResetFences(device(), 1, &frameContext.frameFence) != VK_SUCCESS) {
        ARKOSE_LOG(Error, "VulkanBackend: error resetting frame fence.");
    }
}

void VulkanBackend::newFrame()
{
    SCOPED_PROFILE_ZONE_BACKEND();

    ImGui_ImplVulkan_NewFrame();
}

bool VulkanBackend::executeFrame(RenderPipeline& renderPipeline, float elapsedTime, float deltaTime)
{
    SCOPED_PROFILE_ZONE_BACKEND();

    bool isRelativeFirstFrame = m_relativeFrameIndex < m_frameContexts.size();
    AppState appState { m_swapchainExtent, deltaTime, elapsedTime, m_currentFrameIndex, isRelativeFirstFrame };

    uint32_t frameContextIndex = m_currentFrameIndex % m_frameContexts.size();
    FrameContext& frameContext = *m_frameContexts[frameContextIndex];

    // NOTE: We're ignoring any time spent waiting for the fence, as that would factor e.g. GPU time & sync into the CPU time
    double cpuFrameStartTime = System::get().timeSinceStartup();

    // Processing deferred deletions
    {
        SCOPED_PROFILE_ZONE_BACKEND_NAMED("Processing deferred deletions");

        std::vector<DeleteRequest> const& deleteRequests = m_pendingDeletes[frameContextIndex];

        for (DeleteRequest const& request : deleteRequests) {
            switch (request.type) {
            case VK_OBJECT_TYPE_BUFFER:
                vmaDestroyBuffer(globalAllocator(), static_cast<VkBuffer>(request.vulkanObject), request.allocation);
                break;
            case VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR:
                rayTracingKHR().vkDestroyAccelerationStructureKHR(device(), static_cast<VkAccelerationStructureKHR>(request.vulkanObject), nullptr);
                break;
            default:
                ARKOSE_ERROR("VulkanBackend: unsupported delete request type {}, ignoring", request.type);
            }
        }

        m_pendingDeletes[frameContextIndex].clear();
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
            ARKOSE_LOG(Warning, "VulkanBackend: next image was acquired but it's suboptimal, ignoring.");
        }

        if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
            ARKOSE_LOG(Error, "VulkanBackend: error acquiring next swapchain image.");
        }
    }

    SwapchainImageContext& swapchainImageContext = *m_swapchainImageContexts[swapchainImageIndex].get();

    // We've just found out what image & view we should use for this frame, so send them to the placeholder texture so it knows to bind them
    m_placeholderSwapchainTexture->image = swapchainImageContext.image;
    m_placeholderSwapchainTexture->imageView = swapchainImageContext.imageView;

    // If we wrote any timestamps last time we processed this FrameContext, read and validate those results now
    if (frameContext.numTimestampsWrittenLastTime > 0) {
        VkResult timestampGetQueryResults = vkGetQueryPoolResults(device(), frameContext.timestampQueryPool, 0, frameContext.numTimestampsWrittenLastTime, sizeof(frameContext.timestampResults), frameContext.timestampResults, sizeof(TimestampResult64), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        if (timestampGetQueryResults == VK_SUCCESS || timestampGetQueryResults == VK_NOT_READY) {
            // Validate that all timestamps that we have written to have valid results ready to read
            for (uint32_t startIdx = 0; startIdx < frameContext.numTimestampsWrittenLastTime; startIdx += 2) {
                uint32_t endIdx = startIdx + 1;
                if (frameContext.timestampResults[startIdx].available == 0 || frameContext.timestampResults[endIdx].available == 0) {
                    ARKOSE_LOG(Error, "VulkanBackend: timestamps not available (this probably shouldn't happen?)");
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
        renderPipeline.timer().reportGpuTime(gpuFrameElapsedTime);

        VkCommandBufferBeginInfo commandBufferBeginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        commandBufferBeginInfo.flags = 0u;
        commandBufferBeginInfo.pInheritanceInfo = nullptr;

        VkCommandBuffer commandBuffer = frameContext.commandBuffer;
        if (vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo) != VK_SUCCESS) {
            ARKOSE_LOG(Error, "VulkanBackend: error beginning command buffer command!");
        }

        {
            // Transition swapchain image to attachment-optimal layout

            VkImageMemoryBarrier imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            imageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

            imageBarrier.image = swapchainImageContext.image;
            imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imageBarrier.subresourceRange.baseMipLevel = 0;
            imageBarrier.subresourceRange.levelCount = 1;
            imageBarrier.subresourceRange.baseArrayLayer = 0;
            imageBarrier.subresourceRange.layerCount = 1;

            imageBarrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            imageBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

            vkCmdPipelineBarrier(commandBuffer,
                                 VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                 0,
                                 0, nullptr,
                                 0, nullptr,
                                 1, &imageBarrier);

            m_placeholderSwapchainTexture->currentLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }

        m_currentlyExecutingMainCommandBuffer = true;

        UploadBuffer& uploadBuffer = *frameContext.uploadBuffer;
        uploadBuffer.reset();

        Registry& registry = *m_pipelineRegistry;
        VulkanCommandList cmdList { *this, commandBuffer };

        vkCmdResetQueryPool(commandBuffer, frameContext.timestampQueryPool, 0, FrameContext::TimestampQueryPoolCount);
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, frameContext.timestampQueryPool, frameStartTimestampIdx);

        {
            SCOPED_PROFILE_ZONE_GPU(commandBuffer, "Frame Render Pipeline");
            renderPipeline.forEachNodeInResolvedOrder(registry, [&](RenderPipelineNode& node, const RenderPipelineNode::ExecuteCallback& nodeExecuteCallback) {

                std::string nodeName = node.name();

                SCOPED_PROFILE_ZONE_DYNAMIC(nodeName, 0x00ffff);
                double cpuStartTime = System::get().timeSinceStartup();

                // NOTE: This works assuming we never modify the list of nodes (add/remove/reorder)
                uint32_t nodeStartTimestampIdx = nextTimestampQueryIdx++;
                uint32_t nodeEndTimestampIdx = nextTimestampQueryIdx++;
                node.timer().reportGpuTime(elapsedSecondsBetweenTimestamps(nodeStartTimestampIdx, nodeEndTimestampIdx));

                cmdList.beginDebugLabel(nodeName);
                vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, frameContext.timestampQueryPool, nodeStartTimestampIdx);

                nodeExecuteCallback(appState, cmdList, uploadBuffer);
                cmdList.endNode({});

                vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, frameContext.timestampQueryPool, nodeEndTimestampIdx);
                cmdList.endDebugLabel();

                double cpuElapsed = System::get().timeSinceStartup() - cpuStartTime;
                node.timer().reportCpuTime(cpuElapsed);
            });
        }

        cmdList.beginDebugLabel("GUI");
        {
            SCOPED_PROFILE_ZONE_GPU(commandBuffer, "GUI");
            SCOPED_PROFILE_ZONE_BACKEND_NAMED("GUI Rendering");

            ImGui::Render();
            renderDearImguiFrame(commandBuffer, frameContext, swapchainImageContext);
            
            if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
            }
        }
        cmdList.endDebugLabel();

        {
            // Transition swapchain image to present layout

            VkImageMemoryBarrier imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            imageBarrier.oldLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
            imageBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

            imageBarrier.image = swapchainImageContext.image;
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
        ARKOSE_ASSERT(frameContext.numTimestampsWrittenLastTime < FrameContext::TimestampQueryPoolCount);

        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
            ARKOSE_LOG(Error, "VulkanBackend: error ending command buffer command!");
        }

        m_currentlyExecutingMainCommandBuffer = false;
    }

    // NOTE: We're ignoring any time relatig to TracyVk and also submitting & presenting, as that would factor e.g. GPU time & sync into the CPU time
    double cpuFrameElapsedTime = System::get().timeSinceStartup() - cpuFrameStartTime;
    renderPipeline.timer().reportCpuTime(cpuFrameElapsedTime);

    #if defined(TRACY_ENABLE)
    {
        if (m_currentFrameIndex % TracyVulkanSubmitRate == 0) {
            SCOPED_PROFILE_ZONE_BACKEND_NAMED("Submitting for VkTracy");

            VkCommandBufferBeginInfo beginInfo { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            if (vkBeginCommandBuffer(m_tracyCommandBuffer, &beginInfo) != VK_SUCCESS) {
                ARKOSE_LOG(Fatal, "VulkanBackend: could not begin the command buffer for TracyVkCollect.");
            }

            TracyVkCollect(m_tracyVulkanContext, m_tracyCommandBuffer);

            if (vkEndCommandBuffer(m_tracyCommandBuffer) != VK_SUCCESS) {
                ARKOSE_LOG(Fatal, "VulkanBackend: could not end the command buffer for TracyVkCollect.");
            }

            VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &m_tracyCommandBuffer;

            if (vkQueueSubmit(m_graphicsQueue.queue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
                ARKOSE_LOG(Fatal, "VulkanBackend: could not submit the command buffer for TracyVkCollect.");
            }
        }
    }
    #endif

    if (hasEnabledDeviceExtension(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME) && m_currentFrameIndex % VramStatsQueryRate == 0) {
        SCOPED_PROFILE_ZONE_BACKEND_NAMED("Querying GPU memory budget");
        //ARKOSE_LOG(Verbose, "Querying GPU memory heaps (use / budget):");

        VkPhysicalDeviceMemoryBudgetPropertiesEXT budgetProperties { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT };
        VkPhysicalDeviceMemoryProperties2 memoryProperties2 { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2 };
        memoryProperties2.pNext = &budgetProperties;
        vkGetPhysicalDeviceMemoryProperties2(physicalDevice(), &memoryProperties2);

        VramStats stats {};

        for (uint32_t heapIdx = 0; heapIdx < VK_MAX_MEMORY_HEAPS; ++heapIdx) {
            VkDeviceSize heapBudget = budgetProperties.heapBudget[heapIdx];
            VkDeviceSize heapUsage = budgetProperties.heapUsage[heapIdx];
            if (heapBudget > 0) {

                ARKOSE_ASSERT(heapIdx < memoryProperties2.memoryProperties.memoryHeapCount);
                VkMemoryHeap heap = memoryProperties2.memoryProperties.memoryHeaps[heapIdx];
                bool deviceLocalHeap = (heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0;

                stats.heaps.push_back(VramStats::MemoryHeap { .used = heapUsage,
                                                              .available = heapBudget,
                                                              .deviceLocal = deviceLocalHeap });

                stats.totalUsed += heapUsage;


                //ARKOSE_LOG(Verbose, " heap{}: {:.2f} / {:.2f} GB", heapIdx, conversion::to::GB(heapUsage), conversion::to::GB(heapBudget));
            }
        }

        for (size_t i = 0; i < memoryProperties2.memoryProperties.memoryTypeCount; ++i) {
            VkMemoryType memoryType = memoryProperties2.memoryProperties.memoryTypes[i];
            VramStats::MemoryHeap& heapStats = stats.heaps[memoryType.heapIndex];

            if (memoryType.propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
                ARKOSE_ASSERT(heapStats.deviceLocal);
            }
            
            if (memoryType.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
                heapStats.hostVisible = true;
            }

            if (memoryType.propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) {
                heapStats.hostCoherent = true;
            }
        }

        //ARKOSE_LOG(Verbose, "Total GPU memory usage: {:.2f} GB", conversion::to::GB(stats.totalUsed));

        m_lastQueriedVramStats = stats;
    }

    // Submit queue
    {
        SCOPED_PROFILE_ZONE_BACKEND_NAMED("Submitting for queue");

        VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };

        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &frameContext.commandBuffer;

        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &frameContext.imageAvailableSemaphore;
        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        submitInfo.pWaitDstStageMask = waitStages;

        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &swapchainImageContext.submitSemaphore;

        VkResult submitStatus = vkQueueSubmit(m_graphicsQueue.queue, 1, &submitInfo, frameContext.frameFence);
        if (submitStatus != VK_SUCCESS) {
            ARKOSE_LOG(Error, "VulkanBackend: could not submit the graphics queue.");
        }
    }

    // Present results (synced on the semaphores)
    {
        SCOPED_PROFILE_ZONE_BACKEND_NAMED("Presenting swapchain");

        VkPresentInfoKHR presentInfo = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };

        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &swapchainImageContext.submitSemaphore;

        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &m_swapchain;
        presentInfo.pImageIndices = &swapchainImageIndex;

        VkResult presentResult = vkQueuePresentKHR(m_presentQueue.queue, &presentInfo);

        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
            recreateSwapchain();
            reconstructRenderPipelineResources(renderPipeline);
        } else if (presentResult != VK_SUCCESS) {
            ARKOSE_LOG(Error, "VulkanBackend: could not present swapchain (frame {}).", m_currentFrameIndex);
        }
    }

    m_currentFrameIndex += 1;
    m_relativeFrameIndex += 1;

    return true;
}

std::optional<Backend::SubmitStatus> VulkanBackend::submitRenderPipeline(RenderPipeline& renderPipeline, Registry& registry, UploadBuffer& uploadBuffer, char const* debugName)
{
    SCOPED_PROFILE_ZONE_BACKEND();

    double cpuFrameStartTime = System::get().timeSinceStartup();

    VkCommandBufferAllocateInfo commandBufferAllocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    commandBufferAllocateInfo.commandPool = m_defaultCommandPool;
    commandBufferAllocateInfo.commandBufferCount = 1;
    commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    VkCommandBuffer commandBuffer;
    if (vkAllocateCommandBuffers(device(), &commandBufferAllocateInfo, &commandBuffer) != VK_SUCCESS) {
        ARKOSE_LOG(Error, "VulkanBackend: could not create command buffer, exiting.");
        return {};
    }

    VkCommandBufferBeginInfo commandBufferBeginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    commandBufferBeginInfo.flags = 0u;
    commandBufferBeginInfo.pInheritanceInfo = nullptr;

    if (vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo) != VK_SUCCESS) {
        ARKOSE_LOG(Error, "VulkanBackend: error beginning command buffer command!");
        return {};
    }

    uploadBuffer.reset();

    VulkanCommandList cmdList { *this, commandBuffer };

    AppState hackAppState { renderPipeline.renderResolution(), 1.0f / 60.0f, 0.0f, 0, true };

    {
        std::string pipelineLabel;
        if (debugName) {
            pipelineLabel = fmt::format("Render Pipeline '{}'", debugName);
        } else {
            pipelineLabel = "Render Pipeline";
        }

        SCOPED_PROFILE_ZONE_GPU_DYNAMIC(commandBuffer, pipelineLabel);

        renderPipeline.forEachNodeInResolvedOrder(registry, [&](RenderPipelineNode& node, const RenderPipelineNode::ExecuteCallback& nodeExecuteCallback) {
            std::string nodeName = node.name();

            SCOPED_PROFILE_ZONE_DYNAMIC(nodeName, 0x00ffff);
            double cpuStartTime = System::get().timeSinceStartup();

            cmdList.beginDebugLabel(nodeName);

            nodeExecuteCallback(hackAppState, cmdList, uploadBuffer);
            cmdList.endNode({});

            cmdList.endDebugLabel();

            double cpuElapsed = System::get().timeSinceStartup() - cpuStartTime;
            node.timer().reportCpuTime(cpuElapsed);
        });
    }

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        ARKOSE_LOG(Error, "VulkanBackend: error ending command buffer command!");
        return {};
    }

    double cpuFrameElapsedTime = System::get().timeSinceStartup() - cpuFrameStartTime;
    renderPipeline.timer().reportCpuTime(cpuFrameElapsedTime);

    // NOTE: This fence will be leaked if it's never waited on or polled for completion (so ensure that's done)
    VkFence submitFence = VK_NULL_HANDLE;

    VkFenceCreateInfo fenceCreateInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    if (vkCreateFence(device(), &fenceCreateInfo, nullptr, &submitFence) != VK_SUCCESS) {
        ARKOSE_LOG(Error, "VulkanBackend: could not create execution fence, exiting.");
        return {};
    }

    // Submit queue
    {
        SCOPED_PROFILE_ZONE_BACKEND_NAMED("Submitting for queue");

        VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };

        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        submitInfo.waitSemaphoreCount = 0;
        submitInfo.pWaitSemaphores = nullptr;
        submitInfo.pWaitDstStageMask = nullptr;

        submitInfo.signalSemaphoreCount = 0;
        submitInfo.pSignalSemaphores = nullptr;

        VkResult submitStatus = vkQueueSubmit(m_graphicsQueue.queue, 1, &submitInfo, submitFence);
        if (submitStatus != VK_SUCCESS) {
            ARKOSE_LOG(Error, "VulkanBackend: could not submit to the graphics queue.");
            return {};
        }
    }

    SubmitStatus submitStatus;

    static_assert(sizeof(VkFence) == sizeof(void*));
    submitStatus.data = reinterpret_cast<void*>(submitFence);

    return submitStatus;
}

bool VulkanBackend::pollSubmissionStatus(SubmitStatus& submitStatus) const
{
    if (submitStatus.data == nullptr) {

        // We've already checked for completion and subsequently cleaned up the fence
        return true;

    } else {

        VkFence submitFence = reinterpret_cast<VkFence>(submitStatus.data);

        VkResult status = vkGetFenceStatus(device(), submitFence);
        ARKOSE_ASSERT(status == VK_SUCCESS || status == VK_NOT_READY);
        bool completed = status == VK_SUCCESS;

        if (completed) {
            vkDestroyFence(device(), submitFence, nullptr);
            submitStatus.data = nullptr;
        }

        return completed;
    }
}

bool VulkanBackend::waitForSubmissionCompletion(SubmitStatus& submitStatus, u64 timeout) const
{
    if (submitStatus.data == nullptr) {

        // We've already checked for completion and subsequently cleaned up the fence
        return true;

    } else {

        VkFence submitFence = reinterpret_cast<VkFence>(submitStatus.data);

        VkResult status = vkWaitForFences(device(), 1, &submitFence, VK_TRUE, timeout);
        ARKOSE_ASSERT(status == VK_SUCCESS || status == VK_TIMEOUT);
        bool completed = status == VK_SUCCESS;

        if (completed) {
            vkDestroyFence(device(), submitFence, nullptr);
            submitStatus.data = nullptr;
        }

        return completed;
    }
}

std::optional<VramStats> VulkanBackend::vramStats()
{
    return m_lastQueriedVramStats;
}

bool VulkanBackend::hasDLSSSupport() const
{
#if WITH_DLSS
    return m_dlss != nullptr && m_dlss->isReadyToUse();
#else
    return false;
#endif
}

Extent2D VulkanBackend::queryDLSSRenderResolution(Extent2D outputResolution, UpscalingQuality upscalingQuality) const
{
#if WITH_DLSS
    if (m_dlss && m_dlss->isReadyToUse()) {
        DLSSPreferences dlssPreferences = m_dlss->queryOptimalSettings(outputResolution, upscalingQuality);
        return dlssPreferences.preferredRenderResolution;
    }
#endif

    ARKOSE_LOG(Error, "VulkanBackend: cannot query DLSS render resolution when DLSS is not available, returning output resolution as-is.");
    return outputResolution;
}

void VulkanBackend::renderPipelineDidChange(RenderPipeline& renderPipeline)
{
    reconstructRenderPipelineResources(renderPipeline);
}

void VulkanBackend::shadersDidRecompile(std::vector<std::filesystem::path> const& shaderNames, RenderPipeline& renderPipeline)
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
    ARKOSE_ASSERT(numFrameManagers == NumInFlightFrames);

    Texture* outputTexture = m_placeholderSwapchainTexture.get();

    Registry* previousRegistry = m_pipelineRegistry.get();
    Registry* registry = new Registry(*this, outputTexture, previousRegistry);

    Extent2D framebufferExtent = System::get().windowFramebufferSize();
    renderPipeline.setOutputResolution(framebufferExtent);

    renderPipeline.constructAll(*registry);

    m_pipelineRegistry.reset(registry);

    m_relativeFrameIndex = 0;
}

bool VulkanBackend::issueSingleTimeCommand(const std::function<void(VkCommandBuffer)>& callback) const
{
    if (m_currentlyExecutingMainCommandBuffer && vulkanVerboseDebugMessages)
        ARKOSE_LOG(Warning, "Issuing single-time command while also \"inside\" the main command buffer. This will cause a stall which "
                   "can be avoided by e.g. using UploadBuffer to stage multiple uploads and copy them over on one go.");

    VkCommandBufferAllocateInfo commandBufferAllocInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    commandBufferAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandBufferAllocInfo.commandPool = m_transientCommandPool;
    commandBufferAllocInfo.commandBufferCount = 1;

    VkCommandBuffer oneTimeCommandBuffer;
    vkAllocateCommandBuffers(device(), &commandBufferAllocInfo, &oneTimeCommandBuffer);
    ark::AtScopeExit cleanUpOneTimeUseBuffer([&] {
        vkFreeCommandBuffers(device(), m_transientCommandPool, 1, &oneTimeCommandBuffer);
    });

    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(oneTimeCommandBuffer, &beginInfo) != VK_SUCCESS) {
        ARKOSE_LOG(Error, "VulkanBackend: could not begin the command buffer.");
        return false;
    }

    callback(oneTimeCommandBuffer);

    if (vkEndCommandBuffer(oneTimeCommandBuffer) != VK_SUCCESS) {
        ARKOSE_LOG(Error, "VulkanBackend: could not end the command buffer.");
        return false;
    }

    VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &oneTimeCommandBuffer;

    if (vkQueueSubmit(m_graphicsQueue.queue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
        ARKOSE_LOG(Error, "VulkanBackend: could not submit the single-time command buffer.");
        return false;
    }
    if (vkQueueWaitIdle(m_graphicsQueue.queue) != VK_SUCCESS) {
        ARKOSE_LOG(Error, "VulkanBackend: error while waiting for the graphics queue to idle.");
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
            ARKOSE_LOG(Error, "VulkanBackend: error copying buffer, refer to issueSingleTimeCommand errors for more information.");
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

    VmaAllocationInfo allocationInfo;
    vmaGetAllocationInfo(globalAllocator(), allocation, &allocationInfo);

    VkMemoryType const& memoryType = m_physicalDeviceMemoryProperties.memoryTypes[allocationInfo.memoryType];
    ARKOSE_ASSERT(memoryType.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

    void* mappedMemory = allocationInfo.pMappedData;

    //void* mappedMemory;
    //if (vmaMapMemory(globalAllocator(), allocation, &mappedMemory) != VK_SUCCESS) {
    //    ARKOSE_LOG(Error, "VulkanBackend: could not map staging buffer.");
    //    return false;
    //}

    uint8_t* dst = ((uint8_t*)mappedMemory) + offset;
    std::memcpy(dst, data, size);

    //vmaUnmapMemory(globalAllocator(), allocation);


    if ((memoryType.propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
        vmaFlushAllocation(globalAllocator(), allocation, offset, size);
    }

    return true;
}

bool VulkanBackend::setBufferDataUsingStagingBuffer(VkBuffer buffer, const uint8_t* data, size_t size, size_t offset, VkCommandBuffer* commandBuffer)
{
    SCOPED_PROFILE_ZONE_BACKEND();

    if (size == 0) {
        return true;
    }

    std::unique_ptr<Buffer> stagingBuffer = createBuffer(size, Buffer::Usage::Upload);
    stagingBuffer->mapData(Buffer::MapMode::Write, size, 0, [&](std::byte* mappedMemory) {
        std::memcpy(mappedMemory, data, size);
    });

    VkBuffer stagingVkBuffer = static_cast<VulkanBuffer*>(stagingBuffer.get())->buffer;

    if (!copyBuffer(stagingVkBuffer, buffer, size, offset, commandBuffer)) {
        ARKOSE_LOG(Error, "VulkanBackend: could not copy from staging buffer to buffer.");
        return false;
    }

    return true;
}

std::optional<VkPushConstantRange> VulkanBackend::getPushConstantRangeForShader(const Shader& shader) const
{
    SCOPED_PROFILE_ZONE_BACKEND();

    std::optional<VkPushConstantRange> pushConstantRange;

    for (auto& file : shader.files()) {

        VkShaderStageFlags stageFlag = shaderStageToVulkanShaderStageFlags(file.shaderStage());

        const auto& spv = ShaderManager::instance().spirv(file);
        spirv_cross::Compiler compiler { spv };
        spirv_cross::ShaderResources resources = compiler.get_shader_resources();

        if (!resources.push_constant_buffers.empty()) {
            ARKOSE_ASSERT(resources.push_constant_buffers.size() == 1);
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
                    ARKOSE_LOG(Fatal, "Different push constant sizes in the different shader files!");
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

        VkShaderStageFlags stageFlag = shaderStageToVulkanShaderStageFlags(file.shaderStage());

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
                    ARKOSE_ASSERT(type.array.size() == 1); // i.e. no multidimensional arrays
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
            ARKOSE_ASSERT(resources.push_constant_buffers.size() == 1);
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
                    ARKOSE_LOG(Fatal, "Different push constant sizes in the different shader files!");
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
            ARKOSE_LOG(Fatal, "Error trying to create descriptor set layout from shader");
        }
    }

    return { setLayouts, pushConstantRange };
}

VkShaderStageFlags VulkanBackend::shaderStageToVulkanShaderStageFlags(ShaderStage shaderStage) const
{
    VkShaderStageFlags stageFlags = 0;
    if (isSet(shaderStage & ShaderStage::Vertex))
        stageFlags |= VK_SHADER_STAGE_VERTEX_BIT;
    if (isSet(shaderStage & ShaderStage::Fragment))
        stageFlags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if (isSet(shaderStage & ShaderStage::Compute))
        stageFlags |= VK_SHADER_STAGE_COMPUTE_BIT;
    if (isSet(shaderStage & ShaderStage::RTRayGen))
        stageFlags |= VK_SHADER_STAGE_RAYGEN_BIT_NV;
    if (isSet(shaderStage & ShaderStage::RTMiss))
        stageFlags |= VK_SHADER_STAGE_MISS_BIT_NV;
    if (isSet(shaderStage & ShaderStage::RTClosestHit))
        stageFlags |= VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV;
    if (isSet(shaderStage & ShaderStage::RTAnyHit))
        stageFlags |= VK_SHADER_STAGE_ANY_HIT_BIT_NV;
    if (isSet(shaderStage & ShaderStage::RTIntersection))
        stageFlags |= VK_SHADER_STAGE_INTERSECTION_BIT_NV;
    if (isSet(shaderStage & ShaderStage::Task))
        stageFlags |= VK_SHADER_STAGE_TASK_BIT_EXT;
    if (isSet(shaderStage & ShaderStage::Mesh))
        stageFlags |= VK_SHADER_STAGE_MESH_BIT_EXT;

    ARKOSE_ASSERT(stageFlags != 0);
    return stageFlags;
}

std::vector<VulkanBackend::PushConstantInfo> VulkanBackend::identifyAllPushConstants(const Shader& shader) const
{
    SCOPED_PROFILE_ZONE_BACKEND();

    std::vector<VulkanBackend::PushConstantInfo> infos;

    for (auto& file : shader.files()) {

        const auto& spv = ShaderManager::instance().spirv(file);
        spirv_cross::Compiler compiler { spv };
        spirv_cross::ShaderResources resources = compiler.get_shader_resources();

        if (!resources.push_constant_buffers.empty()) {
            ARKOSE_ASSERT(resources.push_constant_buffers.size() == 1);

            const spirv_cross::Resource& pc_res = resources.push_constant_buffers[0];
            const spirv_cross::SPIRType& pc_type = compiler.get_type(pc_res.type_id);

            // With the NAMED_UNIFORMS macro all push constant blocks will contain exactly one struct with named members
            if (pc_type.member_types.size() != 1) {
                ARKOSE_LOG(Fatal, "identifyAllPushConstants: please use the NAMED_UNIFORMS macro to define push constants!");
            }

            const spirv_cross::TypeID& struct_type_id = pc_type.member_types[0];
            const spirv_cross::SPIRType& struct_type = compiler.get_type(struct_type_id);
            if (struct_type.basetype != spirv_cross::SPIRType::Struct) {
                ARKOSE_LOG(Fatal, "identifyAllPushConstants: please use the NAMED_UNIFORMS macro to define push constants!");
            }

            size_t memberCount = struct_type.member_types.size();
            if (infos.size() > 0 && infos.size() != memberCount) {
                ARKOSE_LOG(Fatal, "identifyAllPushConstants: mismatch in push constant layout (different member counts!)!");
            }

            for (int i = 0; i < memberCount; ++i) {

                const std::string& member_name = compiler.get_member_name(struct_type_id, i);
                size_t offset = compiler.type_struct_member_offset(struct_type, i);
                size_t size = compiler.get_declared_struct_member_size(struct_type, i);

                if (infos.size() == i) {
                    VulkanBackend::PushConstantInfo info;
                    info.name = member_name;
                    info.stages = file.shaderStage();
                    info.offset = (uint32_t)offset;
                    info.size = (int32_t)size;

                    infos.push_back(info);
                }
                else {
                    // We've already seen push constants in another shader file, so just verify there is no mismatch
                    VulkanBackend::PushConstantInfo& existing = infos[i];
                    if (existing.name != member_name || existing.offset != offset || existing.size != size) {
                        ARKOSE_LOG(Fatal, "identifyAllPushConstants: mismatch in push constant layout!");
                    } else {
                        existing.stages = ShaderStage(existing.stages | file.shaderStage());
                    }
                }

            }
        }
    }

    return infos;
}

void VulkanBackend::enqueueForDeletion(VkObjectType type, void* vulkanObject, VmaAllocation allocation)
{
    DeleteRequest deleteRequest { .type = type,
                                  .vulkanObject = vulkanObject,
                                  .allocation = allocation };

    m_pendingDeletes[m_currentFrameIndex % NumInFlightFrames].push_back(deleteRequest);
}

Backend::SwapchainTransferFunction VulkanBackend::swapchainTransferFunction() const
{
    switch (m_surfaceFormat.colorSpace) {
    case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR:
        return SwapchainTransferFunction::sRGB_nonLinear;
    case VK_COLOR_SPACE_HDR10_ST2084_EXT:
        return SwapchainTransferFunction::ST2084;
    default:
        ASSERT_NOT_REACHED();
    }
}

void VulkanBackend::beginRenderDocCapture()
{
    if (m_renderdocAPI) {
        #if PLATFORM_WINDOWS
        m_renderdocAPI->StartFrameCapture(RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(m_instance),
                                          System::get().win32WindowHandle());
        #else
        // TODO: Implement for non-Windows platforms
        #endif
    }
}

void VulkanBackend::endRenderDocCapture()
{
    if (m_renderdocAPI) {
        #if PLATFORM_WINDOWS
        m_renderdocAPI->EndFrameCapture(RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(m_instance),
                                        System::get().win32WindowHandle());
        #else
        // TODO: Implement for non-Windows platforms
        #endif
    }
}
