#pragma once

#include "VulkanCore.h"
#include "VulkanRTX.h"
#include "backend/Backend.h"
#include "backend/vulkan/VulkanResources.h"
#include "rendering/App.h"
#include "rendering/Resources.h"
#include <array>
#include <optional>

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

struct GLFWwindow;

#ifdef NDEBUG
static constexpr bool vulkanDebugMode = true;
#else
static constexpr bool vulkanDebugMode = true;
#endif

class VulkanBackend final : public Backend {
public:
    VulkanBackend(GLFWwindow*, App&);
    ~VulkanBackend() final;

    VulkanBackend(VulkanBackend&&) = delete;
    VulkanBackend(VulkanBackend&) = delete;
    VulkanBackend& operator=(VulkanBackend&) = delete;

    // There might be more elegant ways of giving access. We really don't need everything from here.
    friend class VulkanCommandList;

    ///////////////////////////////////////////////////////////////////////////
    /// Public backend API

    bool hasActiveCapability(Capability) const override;
    bool executeFrame(double elapsedTime, double deltaTime, bool renderGui) override;

    ///////////////////////////////////////////////////////////////////////////
    /// Backend-specific resource types

    std::unique_ptr<Buffer> createBuffer(size_t, Buffer::Usage, Buffer::MemoryHint) override;
    std::unique_ptr<RenderTarget> createRenderTarget(std::vector<RenderTarget::Attachment>) override;
    std::unique_ptr<Texture> createTexture(Extent2D, Texture::Format, Texture::Usage, Texture::MinFilter, Texture::MagFilter, Texture::Mipmap, Texture::Multisampling) override;
    std::unique_ptr<BindingSet> createBindingSet(std::vector<ShaderBinding>) override;
    std::unique_ptr<RenderState> createRenderState(const RenderTarget&, const VertexLayout&, const Shader&, std::vector<const BindingSet*>,
                                                   const Viewport&, const BlendState&, const RasterState&, const DepthState&) override;
    std::unique_ptr<BottomLevelAS> createBottomLevelAccelerationStructure(std::vector<RTGeometry>) override;
    std::unique_ptr<TopLevelAS> createTopLevelAccelerationStructure(std::vector<RTGeometryInstance>) override;
    std::unique_ptr<RayTracingState> createRayTracingState(const ShaderBindingTable& sbt, std::vector<const BindingSet*>, uint32_t maxRecursionDepth) override;
    std::unique_ptr<ComputeState> createComputeState(const Shader&, std::vector<const BindingSet*>) override;

    ///////////////////////////////////////////////////////////////////////////
    /// Utilities

    VmaAllocator& globalAllocator()
    {
        return m_memoryAllocator;
    }

    VkDevice device() const
    {
        return m_core->device();
    }

    VkPhysicalDevice physicalDevice() const
    {
        return m_core->physicalDevice();
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
    void generateMipmaps(const Texture&, VkImageLayout finalLayout); // FIXME: Keet the abstraction level consistent and don't take a Texture& here!

    std::pair<std::vector<VkDescriptorSetLayout>, std::optional<VkPushConstantRange>> createDescriptorSetLayoutForShader(const Shader&) const;

private:
    ///////////////////////////////////////////////////////////////////////////
    /// Capability query metadata & utilities

    struct FeatureInfo {
        bool rtxRayTracing;
        bool shader16BitFloat;
        bool shaderTextureArrayDynamicIndexing;
        bool shaderStorageBufferDynamicIndexing;
        bool advancedValidationFeatures;
    };

    FeatureInfo initFeatureInfo() const;
    mutable std::optional<FeatureInfo> m_featureInfo;

    ///////////////////////////////////////////////////////////////////////////
    /// Command translation & resource management

    void reconstructRenderGraphResources(RenderGraph& renderGraph);

    ///////////////////////////////////////////////////////////////////////////
    /// Drawing

    void drawFrame(const AppState&, double elapsedTime, double deltaTime, bool renderGui, uint32_t swapchainImageIndex);

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
    /// Internal and low level Vulkan resource API. Maybe to be removed at some later time.

    void transitionImageLayoutDEBUG(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, VkImageAspectFlags, VkCommandBuffer) const;

    ///////////////////////////////////////////////////////////////////////////
    /// Window and swapchain related members

    GLFWwindow* m_window;

    VkSwapchainKHR m_swapchain {};
    VulkanQueue m_presentQueue {};

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
    /// Sub-systems

    // TODO: Add swapchain management sub-system?
    std::unique_ptr<VulkanCore> m_core {};
    std::unique_ptr<VulkanRTX> m_rtx {};

    ///////////////////////////////////////////////////////////////////////////
    /// Resource & resource management members

    VmaAllocator m_memoryAllocator;

    App& m_app;

    std::unique_ptr<Registry> m_nodeRegistry {};
    std::vector<std::unique_ptr<Registry>> m_frameRegistries {};

    VulkanQueue m_graphicsQueue {};

    std::vector<VkEvent> m_events {};

    VkCommandPool m_renderGraphFrameCommandPool {};
    VkCommandPool m_transientCommandPool {};

    std::vector<VkCommandBuffer> m_frameCommandBuffers {};
    std::unique_ptr<RenderGraph> m_renderGraph {};

    std::vector<std::unique_ptr<VulkanTexture>> m_swapchainMockColorTextures {};
    std::vector<std::unique_ptr<VulkanRenderTarget>> m_swapchainMockRenderTargets {};

    std::vector<VkDescriptorSetLayout> m_ownedDescriptorSetLayouts;
};
