#pragma once

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
    VulkanBackend(GLFWwindow*, App&);
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
    bool executeFrame(double elapsedTime, double deltaTime) override;

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

    bool hasRtxSupport() const
    {
        return m_rtx != nullptr;
    }

    VulkanRTX& rtx()
    {
        MOOSLIB_ASSERT(hasRtxSupport());
        return *m_rtx;
    }

    ///////////////////////////////////////////////////////////////////////////
    /// Backend services

    bool issueSingleTimeCommand(const std::function<void(VkCommandBuffer)>& callback) const;

    uint32_t findAppropriateMemory(uint32_t typeBits, VkMemoryPropertyFlags properties) const;

    bool copyBuffer(VkBuffer source, VkBuffer destination, VkDeviceSize size, VkCommandBuffer* = nullptr) const;
    bool setBufferMemoryUsingMapping(VmaAllocation, const void* data, VkDeviceSize size);
    bool setBufferDataUsingStagingBuffer(VkBuffer, const void* data, VkDeviceSize size, VkCommandBuffer* = nullptr);

    bool transitionImageLayout(VkImage, bool isDepthFormat, VkImageLayout oldLayout, VkImageLayout newLayout, VkCommandBuffer* = nullptr) const;
    bool copyBufferToImage(VkBuffer, VkImage, uint32_t width, uint32_t height, bool isDepthImage) const;

    std::pair<std::vector<VkDescriptorSetLayout>, std::optional<VkPushConstantRange>> createDescriptorSetLayoutForShader(const Shader&) const;

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
    bool collectAndVerifyCapabilitySupport(App&);

    ///////////////////////////////////////////////////////////////////////////
    /// Command translation & resource management

    void reconstructRenderGraphResources(RenderGraph& renderGraph);

    ///////////////////////////////////////////////////////////////////////////
    /// Drawing

    void drawFrame(const AppState&, double elapsedTime, double deltaTime, uint32_t swapchainImageIndex);

    ///////////////////////////////////////////////////////////////////////////
    /// Swapchain management

    void submitQueue(uint32_t imageIndex, VkSemaphore* waitFor, VkSemaphore* signal, VkFence* inFlight);

    void createSemaphoresAndFences(VkDevice);

    void createAndSetupSwapchain(VkPhysicalDevice, VkDevice, VkSurfaceKHR);
    void destroySwapchain();
    Extent2D recreateSwapchain();

    void setupWindowRenderTargets();
    void createWindowRenderTargetFrontend();

    ///////////////////////////////////////////////////////////////////////////
    /// ImGui related

    void setupDearImgui();
    void destroyDearImgui();

    void updateDearImguiFramebuffers();
    void renderDearImguiFrame(VkCommandBuffer, uint32_t swapchainImageIndex);

    bool m_guiIsSetup { false };
    VkDescriptorPool m_guiDescriptorPool {};
    VkRenderPass m_guiRenderPass {};
    std::vector<VkFramebuffer> m_guiFramebuffers {};

    ///////////////////////////////////////////////////////////////////////////
    /// Vulkan core stuff (e.g. instance, device)

    VkSurfaceFormatKHR pickBestSurfaceFormat() const;
    VkPresentModeKHR pickBestPresentMode() const;
    VkExtent2D pickBestSwapchainExtent() const;
    VkPhysicalDevice pickBestPhysicalDevice() const;

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugMessageCallback(VkDebugUtilsMessageSeverityFlagBitsEXT, VkDebugUtilsMessageTypeFlagsEXT,
                                                               const VkDebugUtilsMessengerCallbackDataEXT*, void* userData);
    VkDebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo() const;
    VkDebugUtilsMessengerEXT createDebugMessenger(VkInstance, VkDebugUtilsMessengerCreateInfoEXT*) const;

    VkInstance createInstance(const std::vector<const char*>& requestedLayers, VkDebugUtilsMessengerCreateInfoEXT*) const;
    VkDevice createDevice(const std::vector<const char*>& requestedLayers, VkPhysicalDevice);
    std::optional<VkDebugUtilsMessengerEXT> m_messenger {};

    VkInstance m_instance {};
    VkPhysicalDevice m_physicalDevice {};
    VkDevice m_device {};

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
    uint32_t m_numSwapchainImages {};

    VkFormat m_swapchainImageFormat {};
    std::vector<VkImage> m_swapchainImages {};
    std::vector<VkImageView> m_swapchainImageViews {};

    std::unique_ptr<VulkanTexture> m_swapchainDepthTexture {};

    std::vector<VkFramebuffer> m_swapchainFramebuffers {};
    VkRenderPass m_swapchainRenderPass {};

    //

    static constexpr size_t maxFramesInFlight { 2 };
    uint32_t m_currentFrameIndex { 0 };

    std::array<VkSemaphore, maxFramesInFlight> m_imageAvailableSemaphores {};
    std::array<VkSemaphore, maxFramesInFlight> m_renderFinishedSemaphores {};
    std::array<VkFence, maxFramesInFlight> m_inFlightFrameFences {};

    ///////////////////////////////////////////////////////////////////////////
    /// Sub-systems / extensions

    std::unique_ptr<VulkanRTX> m_rtx {};

    ///////////////////////////////////////////////////////////////////////////
    /// Resource & resource management members

    VmaAllocator m_memoryAllocator;

    App& m_app;

    std::unique_ptr<Registry> m_sceneRegistry {};
    std::unique_ptr<Registry> m_nodeRegistry {};
    std::vector<std::unique_ptr<Registry>> m_frameRegistries {};

    std::vector<VkEvent> m_events {};

    VkCommandPool m_renderGraphFrameCommandPool {};
    VkCommandPool m_transientCommandPool {};

    std::vector<VkCommandBuffer> m_frameCommandBuffers {};
    std::unique_ptr<RenderGraph> m_renderGraph {};

    std::vector<std::unique_ptr<VulkanTexture>> m_swapchainMockColorTextures {};
    std::vector<std::unique_ptr<VulkanRenderTarget>> m_swapchainMockRenderTargets {};
};
