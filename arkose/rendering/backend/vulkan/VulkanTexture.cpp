#include "VulkanTexture.h"

#include "rendering/backend/vulkan/VulkanBackend.h"
#include "rendering/backend/vulkan/VulkanCommandList.h"
#include <ark/defer.h>
#include "utility/Profiling.h"
#include "core/Logging.h"
#include <stb_image.h>

#include <backends/imgui_impl_vulkan.h>

VulkanTexture::VulkanTexture(Backend& backend, Description desc)
    : Texture(backend, desc)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    bool storageCapable = true;
    bool attachmentCapable = true;

    switch (format()) {
    case Texture::Format::R8:
        vkFormat = VK_FORMAT_R8_UNORM;
        break;
    case Texture::Format::RGBA8:
        vkFormat = VK_FORMAT_R8G8B8A8_UNORM;
        break;
    case Texture::Format::sRGBA8:
        vkFormat = VK_FORMAT_R8G8B8A8_SRGB;
        storageCapable = false;
        break;
    case Texture::Format::R16F:
        vkFormat = VK_FORMAT_R16_SFLOAT;
        break;
    case Texture::Format::R32F:
        vkFormat = VK_FORMAT_R32_SFLOAT;
        break;
    case Texture::Format::RG16F:
        vkFormat = VK_FORMAT_R16G16_SFLOAT;
        break;
    case Texture::Format::RG32F:
        vkFormat = VK_FORMAT_R32G32_SFLOAT;
        break;
    case Texture::Format::RGBA16F:
        vkFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        break;
    case Texture::Format::RGBA32F:
        vkFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
        break;
    case Texture::Format::Depth32F:
        vkFormat = VK_FORMAT_D32_SFLOAT;
        storageCapable = false;
        break;
    case Texture::Format::Depth24Stencil8:
        vkFormat = VK_FORMAT_D24_UNORM_S8_UINT;
        storageCapable = false;
        break;
    case Texture::Format::R32Uint:
        vkFormat = VK_FORMAT_R32_UINT;
        break;
    case Texture::Format::BC5:
        vkFormat = VK_FORMAT_BC5_UNORM_BLOCK;
        storageCapable = false;
        attachmentCapable = false;
        break;
    case Texture::Format::BC7:
        vkFormat = VK_FORMAT_BC7_UNORM_BLOCK;
        storageCapable = false;
        attachmentCapable = false;
        break;
    case Texture::Format::BC7sRGB:
        vkFormat = VK_FORMAT_BC7_SRGB_BLOCK;
        storageCapable = false;
        attachmentCapable = false;
        break;
    case Texture::Format::Unknown:
        ARKOSE_LOG_FATAL("Trying to create new texture with format Unknown, which is not allowed!");
    default:
        ASSERT_NOT_REACHED();
    }

    // Unless we want to enable the multisampled storage images feature we can't have that.. So let's just not, for now..
    // The Vulkan spec states: If the multisampled storage images feature is not enabled, and usage contains VK_IMAGE_USAGE_STORAGE_BIT, samples must be VK_SAMPLE_COUNT_1_BIT
    if (multisampling() != Texture::Multisampling::None) {
        storageCapable = false;
    }

    VkImageUsageFlags usageFlags = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (storageCapable) {
        usageFlags |= VK_IMAGE_USAGE_STORAGE_BIT;
    }

    if (attachmentCapable) {
        usageFlags |= hasDepthFormat() ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }

    // (if we later want to generate mipmaps we need the ability to use each mip as a src & dst in blitting)
    if (hasMipmaps()) {
        usageFlags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        usageFlags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }

    if constexpr (vulkanDebugMode) {
        // for nsight debugging & similar stuff, which needs access to everything
        usageFlags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        usageFlags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }

    // TODO: For now always keep images in device local memory.
    VmaAllocationCreateInfo allocCreateInfo = {};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VkImageCreateInfo imageCreateInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageCreateInfo.extent = { .width = extent().width(), .height = extent().height(), .depth = 1 };
    imageCreateInfo.mipLevels = mipLevels();
    imageCreateInfo.usage = usageFlags;
    imageCreateInfo.format = vkFormat;
    imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageCreateInfo.samples = static_cast<VkSampleCountFlagBits>(multisampling());
    imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkUsage = usageFlags;

    switch (type()) {
    case Type::Texture2D:
        imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        imageCreateInfo.arrayLayers = arrayCount();
        break;
    case Type::Cubemap:
        imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        imageCreateInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        imageCreateInfo.arrayLayers = 6 * arrayCount();
        break;
    default:
        ASSERT_NOT_REACHED();
    }

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend);

    {
        SCOPED_PROFILE_ZONE_NAMED("vmaCreateImage");
        VmaAllocationInfo allocationInfo;
        if (vmaCreateImage(vulkanBackend.globalAllocator(), &imageCreateInfo, &allocCreateInfo, &image, &allocation, &allocationInfo) != VK_SUCCESS) {
            ARKOSE_LOG(Error, "VulkanBackend::newTexture(): could not create image.");
        }
        m_sizeInMemory = allocationInfo.size;
    }

    imageView = createImageView(0, mipLevels(), {});

    VkFilter vkMinFilter;
    switch (minFilter()) {
    case Texture::MinFilter::Linear:
        vkMinFilter = VK_FILTER_LINEAR;
        break;
    case Texture::MinFilter::Nearest:
        vkMinFilter = VK_FILTER_NEAREST;
        break;
    default:
        vkMinFilter = VK_FILTER_MAX_ENUM;
        ASSERT_NOT_REACHED();
    }

    VkFilter vkMagFilter;
    switch (magFilter()) {
    case Texture::MagFilter::Linear:
        vkMagFilter = VK_FILTER_LINEAR;
        break;
    case Texture::MagFilter::Nearest:
        vkMagFilter = VK_FILTER_NEAREST;
        break;
    default:
        vkMagFilter = VK_FILTER_MAX_ENUM;
        ASSERT_NOT_REACHED();
    }

    auto wrapModeToAddressMode = [](ImageWrapMode mode) -> VkSamplerAddressMode {
        switch (mode) {
        case ImageWrapMode::Repeat:
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case ImageWrapMode::MirroredRepeat:
            return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case ImageWrapMode::ClampToEdge:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        default:
            ASSERT_NOT_REACHED();
        }
    };

    VkSamplerCreateInfo samplerCreateInfo = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    samplerCreateInfo.unnormalizedCoordinates = VK_FALSE;
    samplerCreateInfo.magFilter = vkMagFilter;
    samplerCreateInfo.minFilter = vkMinFilter;
    samplerCreateInfo.addressModeU = wrapModeToAddressMode(wrapMode().u);
    samplerCreateInfo.addressModeV = wrapModeToAddressMode(wrapMode().v);
    samplerCreateInfo.addressModeW = wrapModeToAddressMode(wrapMode().w);
    samplerCreateInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerCreateInfo.anisotropyEnable = VK_TRUE;
    samplerCreateInfo.maxAnisotropy = 16.0f;
    samplerCreateInfo.compareEnable = VK_FALSE;
    samplerCreateInfo.compareOp = VK_COMPARE_OP_ALWAYS;

    samplerCreateInfo.mipLodBias = 0.0f;
    samplerCreateInfo.minLod = 0.0f;
    switch (mipmap()) {
    case Texture::Mipmap::None:
        samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerCreateInfo.maxLod = 0.0f;
        break;
    case Texture::Mipmap::Nearest:
        samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerCreateInfo.maxLod = static_cast<float>(mipLevels() - 1);
        break;
    case Texture::Mipmap::Linear:
        samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerCreateInfo.maxLod = static_cast<float>(mipLevels() - 1);
        break;
    }

    if (vkCreateSampler(vulkanBackend.device(), &samplerCreateInfo, nullptr, &sampler) != VK_SUCCESS) {
        ARKOSE_LOG(Error, "VulkanBackend::newTexture(): could not create sampler for the image.");
    }

    currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}

VulkanTexture::~VulkanTexture()
{
    if (!hasBackend())
        return;
    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());

    if (descriptorSetForImGui) {
        vkDestroyImageView(vulkanBackend.device(), imageViewNoAlphaForImGui, nullptr);
        // TODO: Call this, but it's not yet available in the current version of the API.
        // However, it's a pooled resource anyway so we don't stricly need to remove this.
        //ImGui_ImplVulkan_RemoveTexture(..)
    }

    vkDestroySampler(vulkanBackend.device(), sampler, nullptr);
    vkDestroyImageView(vulkanBackend.device(), imageView, nullptr);
    vmaDestroyImage(vulkanBackend.globalAllocator(), image, allocation);
}

void VulkanTexture::setName(const std::string& name)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    Resource::setName(name);

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());
    if (vulkanBackend.hasDebugUtilsSupport()) {

        std::string imageViewName = name + "-view";
        std::string samplerName = name + "-sampler";

        {
            VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
            nameInfo.objectType = VK_OBJECT_TYPE_IMAGE;
            nameInfo.objectHandle = reinterpret_cast<uint64_t>(image);
            nameInfo.pObjectName = name.c_str();

            if (vulkanBackend.debugUtils().vkSetDebugUtilsObjectNameEXT(vulkanBackend.device(), &nameInfo) != VK_SUCCESS) {
                ARKOSE_LOG(Warning, "Could not set debug name for vulkan image resource.");
            }
        }

        {
            VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
            nameInfo.objectType = VK_OBJECT_TYPE_IMAGE_VIEW;
            nameInfo.objectHandle = reinterpret_cast<uint64_t>(imageView);
            nameInfo.pObjectName = imageViewName.c_str();

            if (vulkanBackend.debugUtils().vkSetDebugUtilsObjectNameEXT(vulkanBackend.device(), &nameInfo) != VK_SUCCESS) {
                ARKOSE_LOG(Warning, "Could not set debug name for vulkan image view resource.");
            }
        }

        {
            VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
            nameInfo.objectType = VK_OBJECT_TYPE_SAMPLER;
            nameInfo.objectHandle = reinterpret_cast<uint64_t>(sampler);
            nameInfo.pObjectName = samplerName.c_str();

            if (vulkanBackend.debugUtils().vkSetDebugUtilsObjectNameEXT(vulkanBackend.device(), &nameInfo) != VK_SUCCESS) {
                ARKOSE_LOG(Warning, "Could not set debug name for vulkan sampler resource.");
            }
        }
    }
}

void VulkanTexture::clear(ClearColor color)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());

    // TODO: Support depth texture clears!
    ARKOSE_ASSERT(!hasDepthFormat());

    std::optional<VkImageLayout> originalLayout;
    if (currentLayout != VK_IMAGE_LAYOUT_GENERAL && currentLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        originalLayout = currentLayout;

        VkImageMemoryBarrier imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        imageBarrier.oldLayout = originalLayout.value();
        imageBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        imageBarrier.image = image;
        imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageBarrier.subresourceRange.baseMipLevel = 0;
        imageBarrier.subresourceRange.levelCount = mipLevels();
        imageBarrier.subresourceRange.baseArrayLayer = 0;
        imageBarrier.subresourceRange.layerCount = layerCount();

        // FIXME: Probably overly aggressive barriers!

        VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        imageBarrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

        VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

        bool success = vulkanBackend.issueSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
            vkCmdPipelineBarrier(commandBuffer,
                                 sourceStage, destinationStage, 0,
                                 0, nullptr,
                                 0, nullptr,
                                 1, &imageBarrier);
        });
        if (!success) {
            ARKOSE_LOG(Error, "Could not transition image to general layout.");
            return;
        }
    }

    VkClearColorValue clearValue {};
    clearValue.float32[0] = color.r;
    clearValue.float32[1] = color.g;
    clearValue.float32[2] = color.b;
    clearValue.float32[3] = color.a;

    VkImageSubresourceRange range {};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    range.baseMipLevel = 0;
    range.levelCount = mipLevels();

    range.baseArrayLayer = 0;
    range.layerCount = layerCount();

    bool success = vulkanBackend.issueSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
        vkCmdClearColorImage(commandBuffer, image, VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &range);
    });
    if (!success) {
        ARKOSE_LOG(Error, "Could not clear the color image.");
        return;
    }

    if (originalLayout.has_value() && originalLayout.value() != VK_IMAGE_LAYOUT_UNDEFINED && originalLayout.value() != VK_IMAGE_LAYOUT_PREINITIALIZED) {
        VkImageMemoryBarrier imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        imageBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageBarrier.newLayout = originalLayout.value();
        imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        imageBarrier.image = image;
        imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageBarrier.subresourceRange.baseMipLevel = 0;
        imageBarrier.subresourceRange.levelCount = mipLevels();
        imageBarrier.subresourceRange.baseArrayLayer = 0;
        imageBarrier.subresourceRange.layerCount = layerCount();

        // FIXME: Probably overly aggressive barriers!

        VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        imageBarrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

        VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

        bool success = vulkanBackend.issueSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
            vkCmdPipelineBarrier(commandBuffer,
                                 sourceStage, destinationStage, 0,
                                 0, nullptr,
                                 0, nullptr,
                                 1, &imageBarrier);
        });
        if (!success) {
            ARKOSE_LOG(Error, "Could not transition image back to original layout.");
            return;
        }
    }
}

void VulkanTexture::setData(const void* data, size_t size, size_t mipIdx, size_t arrayIdx)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());

    VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferCreateInfo.size = size;

    VmaAllocationCreateInfo allocCreateInfo = {};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;
    if (vmaCreateBuffer(vulkanBackend.globalAllocator(), &bufferCreateInfo, &allocCreateInfo, &stagingBuffer, &stagingAllocation, nullptr) != VK_SUCCESS) {
        ARKOSE_LOG(Error, "VulkanBackend::updateTexture(): could not create staging buffer.");
    }

    if (!vulkanBackend.setBufferMemoryUsingMapping(stagingAllocation, (uint8_t*)data, size)) {
        ARKOSE_LOG(Error, "VulkanBackend::updateTexture(): could set the buffer memory for the staging buffer.");
        return;
    }

    ark::AtScopeExit cleanUpStagingBuffer([&]() {
        vmaDestroyBuffer(vulkanBackend.globalAllocator(), stagingBuffer, stagingAllocation);
    });

    if (currentLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        VkImageMemoryBarrier imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        imageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        imageBarrier.image = image;
        imageBarrier.subresourceRange.aspectMask = aspectMask();
        imageBarrier.subresourceRange.baseMipLevel = 0;
        imageBarrier.subresourceRange.levelCount = mipLevels();
        imageBarrier.subresourceRange.baseArrayLayer = 0;
        imageBarrier.subresourceRange.layerCount = layerCount();

        VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        imageBarrier.srcAccessMask = 0;

        VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        bool success = vulkanBackend.issueSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
            vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0,
                                 0, nullptr,
                                 0, nullptr,
                                 1, &imageBarrier);
        });
        if (!success) {
            ARKOSE_LOG(Error, "Could not transition the image to transfer optimal layout.");
            return;
        }

        currentLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    }

    Extent2D mipExtent = extentAtMip(narrow_cast<u32>(mipIdx));

    VkBufferImageCopy region = {};

    region.bufferOffset = 0;

    // (zeros here indicate tightly packed data)
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageOffset = VkOffset3D { 0, 0, 0 };
    region.imageExtent = VkExtent3D { mipExtent.width(), mipExtent.height(), 1 };

    region.imageSubresource.aspectMask = aspectMask();
    region.imageSubresource.mipLevel = narrow_cast<u32>(mipIdx);
    region.imageSubresource.baseArrayLayer = arrayIdx;
    region.imageSubresource.layerCount = 1;

    bool copySuccess = vulkanBackend.issueSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
        vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    });

    if (!copySuccess) {
        ARKOSE_LOG(Error, "Could not copy the staging buffer to the image.");
        return;
    }
}

void VulkanTexture::generateMipmaps()
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());

    bool success = vulkanBackend.issueSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
        VulkanCommandList cmdList { vulkanBackend, commandBuffer };
        cmdList.generateMipmaps(*this);
    });

    if (!success) {
        ARKOSE_LOG(Error, "VulkanTexture: error while generating mipmaps");
    }
}

uint32_t VulkanTexture::layerCount() const
{
    switch (type()) {
    case Texture::Type::Texture2D:
        return arrayCount();
    case Texture::Type::Cubemap:
        return 6 * arrayCount();
    default:
        ASSERT_NOT_REACHED();
    }
}

VkImageAspectFlags VulkanTexture::aspectMask() const
{
    VkImageAspectFlags mask = 0u;

    if (hasDepthFormat()) {
        mask |= VK_IMAGE_ASPECT_DEPTH_BIT;
        if (hasStencilFormat())
            mask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    } else {
        mask |= VK_IMAGE_ASPECT_COLOR_BIT;
    }

    return mask;
}

VkImageView VulkanTexture::createImageView(uint32_t baseMip, uint32_t numMips, std::optional<VkComponentMapping> vkComponents) const
{
    VkImageViewCreateInfo viewCreateInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewCreateInfo.image = image;
    viewCreateInfo.format = vkFormat;
    viewCreateInfo.components = vkComponents.value_or(VkComponentMapping { VK_COMPONENT_SWIZZLE_IDENTITY,
                                                                           VK_COMPONENT_SWIZZLE_IDENTITY,
                                                                           VK_COMPONENT_SWIZZLE_IDENTITY,
                                                                           VK_COMPONENT_SWIZZLE_IDENTITY });

    if (hasDepthFormat()) {
        // Create view for the depth aspect only
        viewCreateInfo.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
    } else {
        viewCreateInfo.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_COLOR_BIT;
    }

    ARKOSE_ASSERT(numMips > 0);
    ARKOSE_ASSERT(baseMip < mipLevels());
    ARKOSE_ASSERT(baseMip + numMips - 1 < mipLevels());

    viewCreateInfo.subresourceRange.baseMipLevel = baseMip;
    viewCreateInfo.subresourceRange.levelCount = numMips;

    switch (type()) {
    case Type::Texture2D:
        viewCreateInfo.subresourceRange.baseArrayLayer = 0;
        viewCreateInfo.subresourceRange.layerCount = layerCount();
        viewCreateInfo.viewType = isArray()
            ? VK_IMAGE_VIEW_TYPE_2D_ARRAY
            : VK_IMAGE_VIEW_TYPE_2D;
        break;
    case Type::Cubemap:
        viewCreateInfo.subresourceRange.baseArrayLayer = 0;
        viewCreateInfo.subresourceRange.layerCount = layerCount();
        viewCreateInfo.viewType = isArray()
            ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY
            : VK_IMAGE_VIEW_TYPE_CUBE;
        break;
    default:
        ASSERT_NOT_REACHED();
    }

    VkDevice device = static_cast<const VulkanBackend&>(backend()).device();

    VkImageView imageView {};
    if (vkCreateImageView(device, &viewCreateInfo, nullptr, &imageView) != VK_SUCCESS) {
        ARKOSE_LOG(Fatal, "VulkanBackend: could not create image view.");
    }

    return imageView;
}

std::vector<VulkanTexture*> VulkanTexture::texturesForImGuiRendering {};
ImTextureID VulkanTexture::asImTextureID()
{
    if (descriptorSetForImGui == nullptr) {
        constexpr VkComponentMapping componentMapping = { VK_COMPONENT_SWIZZLE_IDENTITY,
                                                          VK_COMPONENT_SWIZZLE_IDENTITY,
                                                          VK_COMPONENT_SWIZZLE_IDENTITY,
                                                          VK_COMPONENT_SWIZZLE_ONE };
        imageViewNoAlphaForImGui = createImageView(0, 1, componentMapping);
        descriptorSetForImGui = ImGui_ImplVulkan_AddTexture(sampler, imageViewNoAlphaForImGui, ImGuiRenderingTargetLayout);
    }

    // Let the backend handle the potential image layout transision
    texturesForImGuiRendering.push_back(this);

    return static_cast<ImTextureID>(descriptorSetForImGui);
}
