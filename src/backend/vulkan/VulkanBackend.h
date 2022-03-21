#pragma once

#include "backend/base/Backend.h"
#include "backend/Resources.h"
#include "backend/vulkan/VulkanResources.h"
#include "backend/vulkan/extensions/debug-utils/VulkanDebugUtils.h"
#include "backend/vulkan/extensions/ray-tracing-khr/VulkanRayTracingKHR.h"
#include "backend/vulkan/extensions/ray-tracing-nv/VulkanRayTracingNV.h"
#include "rendering/App.h"
#include "utility/AvgElapsedTimer.h"
#include <array>
#include <optional>
#include <unordered_map>

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

struct GLFWwindow;

static constexpr bool vulkanDebugMode = true;
static constexpr bool vulkanVerboseDebugMessages = false;

class VulkanBackend final : public Backend {
public:
    VulkanBackend(Badge<Backend>, GLFWwindow*, const AppSpecification& appSpecification);
    ~VulkanBackend() final;

    VulkanBackend(VulkanBackend&&) = delete;
    VulkanBackend(VulkanBackend&) = delete;
    VulkanBackend& operator=(VulkanBackend&) = delete;

    ///////////////////////////////////////////////////////////////////////////
    /// Public backend API

    bool hasActiveCapability(Capability) const override;

    ShaderDefine rayTracingShaderDefine() const override;

    void renderPipelineDidChange(RenderPipeline&) override;
    void shadersDidRecompile(const std::vector<std::string>& shaderNames, RenderPipeline&) override;

    void shutdown();

    void newFrame();
    bool executeFrame(const Scene&, RenderPipeline&, float elapsedTime, float deltaTime) override;

    ///////////////////////////////////////////////////////////////////////////
    /// Backend-specific resource types

    std::unique_ptr<Buffer> createBuffer(size_t, Buffer::Usage, Buffer::MemoryHint) override;
    std::unique_ptr<RenderTarget> createRenderTarget(std::vector<RenderTarget::Attachment>) override;
    std::unique_ptr<Texture> createTexture(Texture::Description) override;
    std::unique_ptr<BindingSet> createBindingSet(std::vector<ShaderBinding>) override;
    std::unique_ptr<RenderState> createRenderState(const RenderTarget&, const VertexLayout&, const Shader&, const StateBindings&,
                                                   const Viewport&, const BlendState&, const RasterState&, const DepthState&, const StencilState&) override;
    std::unique_ptr<BottomLevelAS> createBottomLevelAccelerationStructure(std::vector<RTGeometry>) override;
    std::unique_ptr<TopLevelAS> createTopLevelAccelerationStructure(uint32_t maxInstanceCount, std::vector<RTGeometryInstance>) override;
    std::unique_ptr<RayTracingState> createRayTracingState(ShaderBindingTable& sbt, const StateBindings&, uint32_t maxRecursionDepth) override;
    std::unique_ptr<ComputeState> createComputeState(const Shader&, std::vector<BindingSet*>) override;

    ///////////////////////////////////////////////////////////////////////////
    /// Utilities

    VmaAllocator& globalAllocator() { return m_memoryAllocator; }
    VkDevice device() const { return m_device; }
    VkPhysicalDevice physicalDevice() const { return m_physicalDevice; }
    VkPipelineCache pipelineCache() const { return m_pipelineCache; }

    enum class RayTracingBackend {
        None,
        NvExtension,
        KhrExtension,
    };

    RayTracingBackend rayTracingBackend() const { return m_rayTracingBackend; }
    bool hasRayTracingSupport() const { return rayTracingBackend() != RayTracingBackend::None; }

    VulkanRayTracingNV& rayTracingNV()
    {
        ASSERT(m_rayTracingBackend == RayTracingBackend::NvExtension && m_rayTracingNv);
        return *m_rayTracingNv;
    }

    const VulkanRayTracingNV& rayTracingNV() const
    {
        ASSERT(m_rayTracingBackend == RayTracingBackend::NvExtension && m_rayTracingNv);
        return *m_rayTracingNv;
    }

    VulkanRayTracingKHR& rayTracingKHR()
    {
        ASSERT(m_rayTracingBackend == RayTracingBackend::KhrExtension && m_rayTracingKhr);
        return *m_rayTracingKhr;
    }

    const VulkanRayTracingKHR& rayTracingKHR() const
    {
        ASSERT(m_rayTracingBackend == RayTracingBackend::KhrExtension && m_rayTracingKhr);
        return *m_rayTracingKhr;
    }

    bool hasDebugUtilsSupport() const { return m_debugUtils != nullptr; }
    VulkanDebugUtils& debugUtils()
    {
        ASSERT(hasDebugUtilsSupport());
        return *m_debugUtils;
    }

    ///////////////////////////////////////////////////////////////////////////
    /// Backend services

    // For being able to detect some cases where we get a full pipeline stall
    bool m_currentlyExecutingMainCommandBuffer = false;

    bool issueSingleTimeCommand(const std::function<void(VkCommandBuffer)>& callback) const;

    uint32_t findAppropriateMemory(uint32_t typeBits, VkMemoryPropertyFlags properties) const;

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

private:
    ///////////////////////////////////////////////////////////////////////////
    /// Capability query metadata & utilities

    std::unordered_set<std::string> m_availableLayers;
    bool hasSupportForLayer(const std::string& name) const;

    std::unordered_set<std::string> m_availableExtensions;
    bool hasSupportForExtension(const std::string& name) const;

    std::unordered_set<std::string> m_availableInstanceExtensions;
    bool hasSupportForInstanceExtension(const std::string& name) const;

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

    void createFrameRenderTargets(const SwapchainImageContext& referenceImageContext);
    void destroyFrameRenderTargets();

    ///////////////////////////////////////////////////////////////////////////
    /// ImGui related

    void setupDearImgui();
    void destroyDearImgui();
    
    void renderDearImguiFrame(VkCommandBuffer, FrameContext&, SwapchainImageContext&);

    bool m_guiIsSetup { false };
    VkDescriptorPool m_guiDescriptorPool {};

    ///////////////////////////////////////////////////////////////////////////
    /// Vulkan core stuff (e.g. instance, device)

    VkSurfaceFormatKHR pickBestSurfaceFormat() const;
    VkPresentModeKHR pickBestPresentMode() const;
    VkExtent2D pickBestSwapchainExtent() const;
    VkPhysicalDevice pickBestPhysicalDevice() const;

    static constexpr const char* piplineCacheFilePath = "assets/.cache/pipeline-cache.bin";
    VkPipelineCache createAndLoadPipelineCacheFromDisk() const;
    void savePipelineCacheToDisk(VkPipelineCache) const;

    VkInstance createInstance(const std::vector<const char*>& requestedLayers, VkDebugUtilsMessengerCreateInfoEXT*) const;
    VkDevice createDevice(const std::vector<const char*>& requestedLayers, VkPhysicalDevice);

    VkDebugUtilsMessengerEXT m_debugMessenger {};
    VkDebugReportCallbackEXT m_debugReportCallback {};

    VkInstance m_instance {};

    VkPhysicalDevice m_physicalDevice {};
    VkPhysicalDeviceProperties m_physicalDeviceProperties {};

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

    GLFWwindow* m_window;
    VkSurfaceKHR m_surface {};

    VkSwapchainKHR m_swapchain {};
    Extent2D m_swapchainExtent {};
    VkFormat m_swapchainImageFormat {};

    struct SwapchainImageContext {
        VkImage image {}; // NOTE: Owned by the swapchain!
        VkImageView imageView {};
        std::unique_ptr<VulkanTexture> mockColorTexture {};
        std::unique_ptr<VulkanTexture> depthTexture {};
    };

    std::vector<std::unique_ptr<SwapchainImageContext>> m_swapchainImageContexts {};

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
        VkSemaphore renderingFinishedSemaphore {};

        VkCommandBuffer commandBuffer {};
        std::unique_ptr<UploadBuffer> uploadBuffer {};

        static constexpr uint32_t TimestampQueryPoolCount = 100;
        TimestampResult64 timestampResults[TimestampQueryPoolCount] = { 0 };
        uint32_t numTimestampsWrittenLastTime { 0 };
        VkQueryPool timestampQueryPool {};
    };

    std::unique_ptr<VulkanRenderTarget> m_clearingRenderTarget {};
    std::unique_ptr<VulkanRenderTarget> m_guiRenderTargetForPresenting {};

    std::array<std::unique_ptr<FrameContext>, NumInFlightFrames> m_frameContexts {};

    ///////////////////////////////////////////////////////////////////////////
    /// Sub-systems / extensions

    RayTracingBackend m_rayTracingBackend { RayTracingBackend::None };
    std::unique_ptr<VulkanRayTracingNV> m_rayTracingNv {};
    std::unique_ptr<VulkanRayTracingKHR> m_rayTracingKhr {};

    std::unique_ptr<VulkanDebugUtils> m_debugUtils {};

    ///////////////////////////////////////////////////////////////////////////
    /// Resource & resource management members

    VmaAllocator m_memoryAllocator;

    std::unique_ptr<Registry> m_pipelineRegistry {};

    VkCommandPool m_defaultCommandPool {};
    VkCommandPool m_transientCommandPool {};

    VkDescriptorSetLayout m_emptyDescriptorSetLayout {};

    AvgElapsedTimer m_frameTimer {};

};
