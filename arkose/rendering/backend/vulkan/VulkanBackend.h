#pragma once

#include "rendering/backend/Resources.h"
#include "rendering/backend/base/Backend.h"
#include "rendering/backend/util/UploadBuffer.h"
#include "rendering/backend/base/UpscalingState.h"
#include "rendering/backend/vulkan/VulkanRenderTarget.h"
#include "rendering/backend/vulkan/VulkanTexture.h"
#include "rendering/backend/vulkan/extensions/debug-utils/VulkanDebugUtils.h"
#include "rendering/backend/vulkan/extensions/mesh-shader-ext/VulkanMeshShaderEXT.h"
#include "rendering/backend/vulkan/extensions/ray-tracing-khr/VulkanRayTracingKHR.h"
#include "rendering/backend/vulkan/extensions/opacity-micromap-ext/VulkanOpacityMicromapEXT.h"
#include "utility/AvgElapsedTimer.h"
#include <array>
#include <optional>
#include <unordered_map>
#include <unordered_set>

#include <renderdoc_app.h>

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#if WITH_DLSS
#include "rendering/backend/vulkan/features/dlss/VulkanDLSS.h"
#endif

#if defined(TRACY_ENABLE)
#include <tracy/TracyVulkan.hpp>
#include "rendering/backend/vulkan/extensions/VulkanProcAddress.h"
#endif

#if defined(ARKOSE_DEBUG)
static constexpr bool vulkanDebugMode = true;
static constexpr bool vulkanVerboseDebugMessages = true;
#elif defined(ARKOSE_DEVELOP)
static constexpr bool vulkanDebugMode = true;
static constexpr bool vulkanVerboseDebugMessages = false;
#elif defined(ARKOSE_RELEASE)
static constexpr bool vulkanDebugMode = false;
static constexpr bool vulkanVerboseDebugMessages = false;
#endif

class VulkanBackend final : public Backend {
public:
    VulkanBackend(Badge<Backend>, const AppSpecification& appSpecification);
    ~VulkanBackend() final;

    VulkanBackend(VulkanBackend&&) = delete;
    VulkanBackend(VulkanBackend&) = delete;
    VulkanBackend& operator=(VulkanBackend&) = delete;

    ///////////////////////////////////////////////////////////////////////////
    /// Public backend API

    bool hasActiveCapability(Capability) const override;

    void renderPipelineDidChange(RenderPipeline&) override;
    void shadersDidRecompile(std::vector<std::filesystem::path> const& shaderNames, RenderPipeline&) override;

    void completePendingOperations() override;

    void newFrame() override;
    bool executeFrame(RenderPipeline&, float elapsedTime, float deltaTime) override;

    std::optional<SubmitStatus> submitRenderPipeline(RenderPipeline&, Registry&, UploadBuffer&, char const* debugName) override;
    bool pollSubmissionStatus(SubmitStatus&) const override;
    bool waitForSubmissionCompletion(SubmitStatus&, u64 timeout) const override;

    int vramStatsReportRate() const override { return VramStatsQueryRate; }
    std::optional<VramStats> vramStats() override;

    bool hasUpscalingSupport() const override;
    UpscalingPreferences queryUpscalingPreferences(UpscalingTech, UpscalingQuality, Extent2D outputRes) const override;

    SwapchainTransferFunction swapchainTransferFunction() const override;

    ///////////////////////////////////////////////////////////////////////////
    /// Backend-specific resource types

    std::unique_ptr<Buffer> createBuffer(size_t, Buffer::Usage) override;
    std::unique_ptr<RenderTarget> createRenderTarget(std::vector<RenderTarget::Attachment>) override;
    std::unique_ptr<Sampler> createSampler(Sampler::Description) override;
    std::unique_ptr<Texture> createTexture(Texture::Description) override;
    std::unique_ptr<BindingSet> createBindingSet(std::vector<ShaderBinding>) override;
    std::unique_ptr<RenderState> createRenderState(const RenderTarget&, const std::vector<VertexLayout>&, const Shader&, const StateBindings&,
                                                   const RasterState&, const DepthState&, const StencilState&) override;
    std::unique_ptr<BottomLevelAS> createBottomLevelAccelerationStructure(std::vector<RTGeometry>) override;
    std::unique_ptr<TopLevelAS> createTopLevelAccelerationStructure(uint32_t maxInstanceCount) override;
    std::unique_ptr<RayTracingState> createRayTracingState(ShaderBindingTable& sbt, const StateBindings&, uint32_t maxRecursionDepth) override;
    std::unique_ptr<ComputeState> createComputeState(Shader const&, StateBindings const&) override;
    std::unique_ptr<UpscalingState> createUpscalingState(UpscalingTech, UpscalingQuality, Extent2D renderRes, Extent2D outputDisplayRes) override;

    ///////////////////////////////////////////////////////////////////////////
    /// Utilities

    VmaAllocator& globalAllocator() { return m_memoryAllocator; }
    VkDevice device() const { return m_device; }
    VkPhysicalDevice physicalDevice() const { return m_physicalDevice; }
    VkPipelineCache pipelineCache() const { return m_pipelineCache; }

    VkPhysicalDeviceProperties const& physicalDeviceProperties() const { return m_physicalDeviceProperties; }
    VkPhysicalDeviceMemoryProperties const& physicalDeviceMemoryProperties() const { return m_physicalDeviceMemoryProperties; }

    VulkanTexture* placeholderSwapchainTexture() const { return m_placeholderSwapchainTexture.get(); }

    bool hasRayTracingSupport() const { return m_rayTracingKhr != nullptr; }

    VulkanRayTracingKHR& rayTracingKHR()
    {
        ARKOSE_ASSERT(hasRayTracingSupport());
        return *m_rayTracingKhr;
    }

    const VulkanRayTracingKHR& rayTracingKHR() const
    {
        ARKOSE_ASSERT(hasRayTracingSupport());
        return *m_rayTracingKhr;
    }

    bool hasMeshShadingSupport() const { return m_meshShaderExt != nullptr; }
    VulkanMeshShaderEXT& meshShaderEXT()
    {
        ARKOSE_ASSERT(hasMeshShadingSupport());
        return *m_meshShaderExt;
    }

    bool hasOpacityMicromapSupport() const { return m_opacityMicromapExt != nullptr; }
    VulkanOpacityMicromapEXT& opacityMicromapEXT()
    {
        ARKOSE_ASSERT(hasOpacityMicromapSupport());
        return *m_opacityMicromapExt;
    }

    bool hasDebugUtilsSupport() const { return m_debugUtils != nullptr; }
    VulkanDebugUtils& debugUtils()
    {
        ARKOSE_ASSERT(hasDebugUtilsSupport());
        return *m_debugUtils;
    }

    #if WITH_DLSS
        bool hasDlssFeature() const
        {
            return m_dlss != nullptr && m_dlss->isReadyToUse();
        }
        VulkanDLSS& dlssFeature()
        {
            ARKOSE_ASSERT(hasDlssFeature());
            return *m_dlss;
        }
    #endif

    #if defined(TRACY_ENABLE)
        tracy::VkCtx* tracyVulkanContext()
        {
            return m_tracyVulkanContext;
        }
    #endif

    ///////////////////////////////////////////////////////////////////////////
    /// Backend services

    // For being able to detect some cases where we get a full pipeline stall
    bool m_currentlyExecutingMainCommandBuffer = false;

    bool issueSingleTimeCommand(const std::function<void(VkCommandBuffer)>& callback) const;

    bool copyBuffer(VkBuffer source, VkBuffer destination, size_t size, size_t dstOffset = 0, VkCommandBuffer* = nullptr) const;
    bool setBufferMemoryUsingMapping(VmaAllocation, const uint8_t* data, size_t size, size_t offset = 0);
    bool setBufferDataUsingStagingBuffer(VkBuffer, const uint8_t* data, size_t size, size_t offset = 0, VkCommandBuffer* = nullptr);

    std::optional<VkPushConstantRange> getPushConstantRangeForShader(const Shader&) const;
    std::pair<std::vector<VkDescriptorSetLayout>, std::optional<VkPushConstantRange>> createDescriptorSetLayoutForShader(const Shader&) const;
    VkDescriptorSetLayout emptyDescriptorSetLayout() const { return m_emptyDescriptorSetLayout; };

    VkShaderStageFlags shaderStageToVulkanShaderStageFlags(ShaderStage) const;

    struct PushConstantInfo {
        std::string name {};
        int32_t offset { 0 };
        int32_t size { 0 };
        ShaderStage stages { 0 };
    };
    std::vector<PushConstantInfo> identifyAllPushConstants(const Shader&) const;

    void enqueueForDeletion(VkObjectType type, void* vulkanObject, VmaAllocation allocation);

private:
    ///////////////////////////////////////////////////////////////////////////
    /// Capability query metadata & utilities

    std::unordered_set<std::string> m_availableLayers;
    bool hasSupportForLayer(const std::string& name) const;

    std::unordered_set<std::string> m_availableDeviceExtensions;
    bool hasSupportForDeviceExtension(const std::string& name) const;

    std::unordered_set<std::string> m_enabledDeviceExtensions;
    bool hasEnabledDeviceExtension(const std::string& name) const;

    std::unordered_set<std::string> m_availableInstanceExtensions;
    bool hasSupportForInstanceExtension(const std::string& name) const;

    std::unordered_set<std::string> m_enabledInstanceExtensions;
    bool hasEnabledInstanceExtension(const std::string& name) const;

    std::unordered_map<Capability, bool> m_activeCapabilities;
    bool collectAndVerifyCapabilitySupport(const AppSpecification&);

    ///////////////////////////////////////////////////////////////////////////
    /// Command translation & resource management

    void reconstructRenderPipelineResources(RenderPipeline& renderPipeline);

    ///////////////////////////////////////////////////////////////////////////
    /// Swapchain management

    struct FrameContext;
    struct SwapchainImageContext;

    void createSwapchain(VkPhysicalDevice, VkDevice, VkSurfaceKHR);
    void destroySwapchain();
    Extent2D recreateSwapchain();

    void createFrameContexts();
    void destroyFrameContexts();

    ///////////////////////////////////////////////////////////////////////////
    /// ImGui related

    void setupDearImgui();
    void destroyDearImgui();

    void renderDearImguiFrame(VkCommandBuffer, FrameContext&, SwapchainImageContext&);

    bool m_guiIsSetup { false };
    VkDescriptorPool m_guiDescriptorPool {};

    std::unique_ptr<VulkanRenderTarget> m_imguiRenderTarget {};

    ///////////////////////////////////////////////////////////////////////////
    /// Vulkan core stuff (e.g. instance, device)

    static constexpr auto VulkanApiVersion = VK_API_VERSION_1_4;

    VkSurfaceFormatKHR pickBestSurfaceFormat() const;
    VkPresentModeKHR pickBestPresentMode() const;
    VkExtent2D pickBestSwapchainExtent() const;
    VkPhysicalDevice pickBestPhysicalDevice() const;

    std::filesystem::path pipelineCacheFilePath = "assets/.cache/pipeline-cache.bin";
    VkPipelineCache createAndLoadPipelineCacheFromDisk() const;
    void savePipelineCacheToDisk(VkPipelineCache) const;

    VkInstance createInstance(const std::vector<const char*>& requestedLayers, VkDebugUtilsMessengerCreateInfoEXT*);
    VkDevice createDevice(const std::vector<const char*>& requestedLayers, VkPhysicalDevice);

    VkDebugUtilsMessengerEXT m_debugMessenger {};

    VkInstance m_instance {};

    VkPhysicalDevice m_physicalDevice {};
    VkPhysicalDeviceProperties m_physicalDeviceProperties {};
    VkPhysicalDeviceMemoryProperties m_physicalDeviceMemoryProperties {};

    bool checkForResizableBARSupport() const;
    bool m_supportsResizableBAR { false };

    VkDevice m_device {};

    VkPipelineCache m_pipelineCache {};

    struct VulkanQueue {
        uint32_t familyIndex;
        VkQueue queue;
    };

    void findQueueFamilyIndices(VkPhysicalDevice, VkSurfaceKHR);

    VulkanQueue m_presentQueue {};
    VulkanQueue m_graphicsQueue {};
    VulkanQueue m_computeQueue {};

    ///////////////////////////////////////////////////////////////////////////
    /// Window and swapchain related members

    VkSurfaceKHR m_surface {};
    VkSurfaceFormatKHR m_surfaceFormat {};

    VkSwapchainKHR m_swapchain {};
    Extent2D m_swapchainExtent {};

    struct SwapchainImageContext {
        VkImage image {}; // NOTE: Owned by the swapchain!
        VkImageView imageView {};
        VkSemaphore submitSemaphore {};
    };

    std::vector<std::unique_ptr<SwapchainImageContext>> m_swapchainImageContexts {};

    std::unique_ptr<VulkanTexture> m_placeholderSwapchainTexture {};

    ///////////////////////////////////////////////////////////////////////////
    /// Frame management related members

    static constexpr int NumInFlightFrames = 2;
    uint32_t m_currentFrameIndex { 0 };
    uint32_t m_relativeFrameIndex { 0 };

    struct TimestampResult64 {
        uint64_t timestamp;
        uint64_t available;
    };

    struct FrameContext {
        VkFence frameFence {};
        VkSemaphore imageAvailableSemaphore {};

        VkCommandBuffer commandBuffer {};
        std::unique_ptr<UploadBuffer> uploadBuffer {};

        static constexpr uint32_t TimestampQueryPoolCount = 100;
        TimestampResult64 timestampResults[TimestampQueryPoolCount] = {};
        uint32_t numTimestampsWrittenLastTime { 0 };
        VkQueryPool timestampQueryPool {};
    };

    std::array<std::unique_ptr<FrameContext>, NumInFlightFrames> m_frameContexts {};

    ///////////////////////////////////////////////////////////////////////////
    /// Sub-systems / extensions

    std::unique_ptr<VulkanRayTracingKHR> m_rayTracingKhr {};

    std::unique_ptr<VulkanMeshShaderEXT> m_meshShaderExt {};

    std::unique_ptr<VulkanOpacityMicromapEXT> m_opacityMicromapExt {};

    std::unique_ptr<VulkanDebugUtils> m_debugUtils {};

    #if WITH_DLSS
        bool m_dlssHasAllRequiredExtensions { true };
        std::unique_ptr<VulkanDLSS> m_dlss {};
    #endif

    ///////////////////////////////////////////////////////////////////////////
    /// Resource & resource management members

    VmaAllocator m_memoryAllocator;

    static constexpr int VramStatsQueryRate = 10;
    std::optional<VramStats> m_lastQueriedVramStats {};

    std::unique_ptr<Registry> m_pipelineRegistry {};

    VkCommandPool m_defaultCommandPool {};
    VkCommandPool m_transientCommandPool {};

    VkDescriptorSetLayout m_emptyDescriptorSetLayout {};

    struct DeleteRequest {
        VkObjectType type;
        void* vulkanObject;
        VmaAllocation allocation;
    };
    std::array<std::vector<DeleteRequest>, NumInFlightFrames> m_pendingDeletes {};

    #if defined(TRACY_ENABLE)
        static constexpr uint32_t TracyVulkanSubmitRate = 10;
        static_assert(TracyVulkanSubmitRate > NumInFlightFrames, "We don't fence the submissions for the Tracy commands; instead we rely on the frame fences");
        tracy::VkCtx* m_tracyVulkanContext {};
        VkCommandBuffer m_tracyCommandBuffer {};
    #endif

    ///////////////////////////////////////////////////////////////////////////
    /// Debug stuff

    RENDERDOC_API_1_6_0* m_renderdocAPI { nullptr };

    void beginRenderDocCapture();
    void endRenderDocCapture();

};
