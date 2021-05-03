#pragma once

#include "VulkanDebugUtils.h"
#include "VulkanRTX.h"
#include "backend/Backend.h"
#include "backend/Resources.h"
#include "backend/vulkan/VulkanResources.h"
#include "rendering/App.h"
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
    VulkanBackend(GLFWwindow*, const AppSpecification& appSpecification);
    ~VulkanBackend() final;

    VulkanBackend(VulkanBackend&&) = delete;
    VulkanBackend(VulkanBackend&) = delete;
    VulkanBackend& operator=(VulkanBackend&) = delete;

    // FIXME: There might be more elegant ways of giving access. We really don't need everything from here.
    //  Currently only VkEvent are accessed privately from the command list.
    friend class VulkanCommandList;

    ///////////////////////////////////////////////////////////////////////////
    /// Public backend API

    bool hasActiveCapability(Capability) const override;

    Registry& getPersistentRegistry() override;

    void renderGraphDidChange(RenderGraph&) override;
    void shadersDidRecompile(const std::vector<std::string>& shaderNames, RenderGraph&) override;

    void newFrame(Scene&);
    bool executeFrame(const Scene&, RenderGraph&, double elapsedTime, double deltaTime) override;

    ///////////////////////////////////////////////////////////////////////////
    /// Backend-specific resource types

    std::unique_ptr<Buffer> createBuffer(size_t, Buffer::Usage, Buffer::MemoryHint) override;
    std::unique_ptr<RenderTarget> createRenderTarget(std::vector<RenderTarget::Attachment>) override;
    std::unique_ptr<Texture> createTexture(Texture::TextureDescription) override;
    std::unique_ptr<BindingSet> createBindingSet(std::vector<ShaderBinding>) override;
    std::unique_ptr<RenderState> createRenderState(const RenderTarget&, const VertexLayout&, const Shader&, std::vector<BindingSet*>,
                                                   const Viewport&, const BlendState&, const RasterState&, const DepthState&) override;
    std::unique_ptr<BottomLevelAS> createBottomLevelAccelerationStructure(std::vector<RTGeometry>) override;
    std::unique_ptr<TopLevelAS> createTopLevelAccelerationStructure(std::vector<RTGeometryInstance>) override;
    std::unique_ptr<RayTracingState> createRayTracingState(ShaderBindingTable& sbt, std::vector<BindingSet*>, uint32_t maxRecursionDepth) override;
    std::unique_ptr<ComputeState> createComputeState(const Shader&, std::vector<BindingSet*>) override;

    ///////////////////////////////////////////////////////////////////////////
    /// Utilities

    VmaAllocator& globalAllocator()
    {
        return m_memoryAllocator;
    }

    VkDevice device() const
    {
        return m_device;
    }

    VkPhysicalDevice physicalDevice() const
    {
        return m_physicalDevice;
    }

    VkPipelineCache pipelineCache() const
    {
        return m_pipelineCache;
    }

    bool hasRtxSupport() const
    {
        return m_rtx != nullptr;
    }

    VulkanRTX& rtx()
    {
        ASSERT(hasRtxSupport());
        return *m_rtx;
    }

    bool hasDebugUtilsSupport() const
    {
        return m_debugUtils != nullptr;
    }

    VulkanDebugUtils& debugUtils()
    {
        ASSERT(hasDebugUtilsSupport());
        return *m_debugUtils;
    }

    ///////////////////////////////////////////////////////////////////////////
    /// Backend services

    bool issueSingleTimeCommand(const std::function<void(VkCommandBuffer)>& callback) const;

    uint32_t findAppropriateMemory(uint32_t typeBits, VkMemoryPropertyFlags properties) const;

    bool copyBuffer(VkBuffer source, VkBuffer destination, size_t size, size_t dstOffset = 0, VkCommandBuffer* = nullptr) const;
    bool setBufferMemoryUsingMapping(VmaAllocation, const uint8_t* data, size_t size, size_t offset = 0);
    bool setBufferDataUsingStagingBuffer(VkBuffer, const uint8_t* data, size_t size, size_t offset = 0, VkCommandBuffer* = nullptr);
    bool copyBufferToImage(VkBuffer, VkImage, uint32_t width, uint32_t height, bool isDepthImage) const;

    std::pair<std::vector<VkDescriptorSetLayout>, std::optional<VkPushConstantRange>> createDescriptorSetLayoutForShader(const Shader&) const;

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

    void reconstructRenderGraphResources(RenderGraph& renderGraph);

    ///////////////////////////////////////////////////////////////////////////
    /// Swapchain management

    struct FrameContext;
    struct SwapchainImageContext;

    void createSwapchain(VkPhysicalDevice, VkDevice, VkSurfaceKHR);
    void destroySwapchain();
    Extent2D recreateSwapchain();

    void createFrameContexts();
    void destroyFrameContexts();
    void createFrameRenderTargets(FrameContext&, const SwapchainImageContext& referenceImageContext);

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
    VkDebugUtilsMessengerEXT m_messenger {};

    VkInstance m_instance {};
    VkPhysicalDevice m_physicalDevice {};
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

    const int m_numInFlightFrames = 2;
    uint32_t m_currentFrameIndex { 0 };
    uint32_t m_relativeFrameIndex { 0 };

    struct FrameContext {
        VkFence frameFence {};
        VkSemaphore imageAvailableSemaphore {};
        VkSemaphore renderingFinishedSemaphore {};

        std::unique_ptr<VulkanRenderTarget> clearingRenderTarget {};
        std::unique_ptr<VulkanRenderTarget> guiRenderTargetForPresenting {};

        VkCommandBuffer commandBuffer {};
        std::unique_ptr<Registry> registry {};
    };

    std::vector<std::unique_ptr<FrameContext>> m_frameContexts {};

    ///////////////////////////////////////////////////////////////////////////
    /// Sub-systems / extensions

    std::unique_ptr<VulkanRTX> m_rtx {};
    std::unique_ptr<VulkanDebugUtils> m_debugUtils {};

    ///////////////////////////////////////////////////////////////////////////
    /// Resource & resource management members

    VmaAllocator m_memoryAllocator;

    std::unique_ptr<Registry> m_persistentRegistry {};
    std::unique_ptr<Registry> m_nodeRegistry {};

    // TODO: Clean up / remove
    std::vector<VkEvent> m_events {};

    VkCommandPool m_defaultCommandPool {};
    VkCommandPool m_transientCommandPool {};

};
