#include "VulkanTexture.h"

#include "backend/vulkan/VulkanBackend.h"
#include "utility/Profiling.h"
#include "utility/Logging.h"
#include <stb_image.h>

VulkanTexture::VulkanTexture(Backend& backend, TextureDescription desc)
    : Texture(backend, desc)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    // HACK: Now we longer specify what usage we want for the texture, and instead always select all
    //  possible capabilities. However, some texture formats (e.g. sRGB formats) do not support being
    //  used as a storage image, so we need to explicitly disable it for those formats.
    bool storageCapable = true;

    switch (format()) {
    case Texture::Format::R32:
        vkFormat = VK_FORMAT_R32_UINT;
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
    case Texture::Format::Unknown:
        LogErrorAndExit("Trying to create new texture with format Unknown, which is not allowed!\n");
    default:
        ASSERT_NOT_REACHED();
    }

    // Unless we want to enable the multisampled storage images feature we can't have that.. So let's just not, for now..
    // The Vulkan spec states: If the multisampled storage images feature is not enabled, and usage contains VK_IMAGE_USAGE_STORAGE_BIT, samples must be VK_SAMPLE_COUNT_1_BIT
    if (multisampling() != Texture::Multisampling::None) {
        storageCapable = false;
    }

    // Since we don't specify usage we have to assume all of them may be used (at least the common operations)
    const VkImageUsageFlags attachmentFlags = hasDepthFormat() ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    VkImageUsageFlags usageFlags = attachmentFlags | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (storageCapable)
        usageFlags |= VK_IMAGE_USAGE_STORAGE_BIT;

    // (if we later want to generate mipmaps we need the ability to use each mip as a src & dst in blitting)
    if (hasMipmaps()) {
        usageFlags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        usageFlags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }

    if (vulkanDebugMode) {
        // for nsight debugging & similar stuff, which needs access to everything
        usageFlags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        usageFlags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
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

    {
        SCOPED_PROFILE_ZONE_NAMED("vmaCreateImage");
        auto& allocator = static_cast<VulkanBackend&>(backend).globalAllocator();
        if (vmaCreateImage(allocator, &imageCreateInfo, &allocCreateInfo, &image, &allocation, nullptr) != VK_SUCCESS) {
            LogError("VulkanBackend::newTexture(): could not create image.\n");
        }
    }

    VkImageAspectFlags aspectFlags = 0u;
    if (hasDepthFormat()) {
        // Create view for the depth aspect only
        aspectFlags |= VK_IMAGE_ASPECT_DEPTH_BIT;
    } else {
        aspectFlags |= VK_IMAGE_ASPECT_COLOR_BIT;
    }

    VkImageViewCreateInfo viewCreateInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewCreateInfo.subresourceRange.aspectMask = aspectFlags;
    viewCreateInfo.image = image;
    viewCreateInfo.format = vkFormat;
    viewCreateInfo.components = {
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY
    };
    viewCreateInfo.subresourceRange.baseMipLevel = 0;
    viewCreateInfo.subresourceRange.levelCount = mipLevels();

    switch (type()) {
    case Type::Texture2D:
        viewCreateInfo.subresourceRange.baseArrayLayer = 0;
        viewCreateInfo.subresourceRange.layerCount = arrayCount();
        viewCreateInfo.viewType = isArray()
            ? VK_IMAGE_VIEW_TYPE_2D_ARRAY
            : VK_IMAGE_VIEW_TYPE_2D;
        break;
    case Type::Cubemap:
        viewCreateInfo.subresourceRange.baseArrayLayer = 0;
        viewCreateInfo.subresourceRange.layerCount = 6 * arrayCount();
        viewCreateInfo.viewType = isArray()
            ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY
            : VK_IMAGE_VIEW_TYPE_CUBE;
        break;
    default:
        ASSERT_NOT_REACHED();
    }

    VkDevice device = static_cast<VulkanBackend&>(backend).device();
    if (vkCreateImageView(device, &viewCreateInfo, nullptr, &imageView) != VK_SUCCESS) {
        LogError("VulkanBackend::newTexture(): could not create image view.\n");
    }

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

    auto wrapModeToAddressMode = [](WrapMode mode) -> VkSamplerAddressMode {
        switch (mode) {
        case WrapMode::Repeat:
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case WrapMode::MirroredRepeat:
            return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case WrapMode::ClampToEdge:
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
        samplerCreateInfo.maxLod = static_cast<float>(mipLevels());
        break;
    case Texture::Mipmap::Linear:
        samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerCreateInfo.maxLod = static_cast<float>(mipLevels());
        break;
    }

    if (vkCreateSampler(device, &samplerCreateInfo, nullptr, &sampler) != VK_SUCCESS) {
        LogError("VulkanBackend::newTexture(): could not create sampler for the image.\n");
    }

    currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}

VulkanTexture::~VulkanTexture()
{
    if (!hasBackend())
        return;
    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());
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
                LogWarning("Could not set debug name for vulkan image resource.\n");
            }
        }

        {
            VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
            nameInfo.objectType = VK_OBJECT_TYPE_IMAGE_VIEW;
            nameInfo.objectHandle = reinterpret_cast<uint64_t>(imageView);
            nameInfo.pObjectName = imageViewName.c_str();

            if (vulkanBackend.debugUtils().vkSetDebugUtilsObjectNameEXT(vulkanBackend.device(), &nameInfo) != VK_SUCCESS) {
                LogWarning("Could not set debug name for vulkan image view resource.\n");
            }
        }

        {
            VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
            nameInfo.objectType = VK_OBJECT_TYPE_SAMPLER;
            nameInfo.objectHandle = reinterpret_cast<uint64_t>(sampler);
            nameInfo.pObjectName = samplerName.c_str();

            if (vulkanBackend.debugUtils().vkSetDebugUtilsObjectNameEXT(vulkanBackend.device(), &nameInfo) != VK_SUCCESS) {
                LogWarning("Could not set debug name for vulkan sampler resource.\n");
            }
        }
    }
}

void VulkanTexture::clear(ClearColor color)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());

    // TODO: Support depth texture clears!
    ASSERT(!hasDepthFormat());

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
            LogError("Could not transition image to general layout.\n");
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
        LogError("Could not clear the color image.\n");
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
            LogError("Could not transition image back to original layout.\n");
            return;
        }
    }
}

void VulkanTexture::setPixelData(vec4 pixel)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    int numChannels;
    bool isHdr = false;

    switch (format()) {
    case Texture::Format::R32:
        numChannels = 1;
        isHdr = false;
        break;
    case Texture::Format::R16F:
    case Texture::Format::R32F:
        numChannels = 1;
        isHdr = true;
        break;
    case Texture::Format::RG16F:
    case Texture::Format::RG32F:
        numChannels = 2;
        isHdr = true;
        break;
    case Texture::Format::RGBA8:
    case Texture::Format::sRGBA8:
        numChannels = 4;
        isHdr = false;
        break;
    case Texture::Format::RGBA16F:
    case Texture::Format::RGBA32F:
        numChannels = 4;
        isHdr = true;
        break;
    case Texture::Format::Depth32F:
        numChannels = 1;
        isHdr = true;
        break;
    case Texture::Format::Unknown:
        ASSERT_NOT_REACHED();
        break;
    }

    ASSERT(numChannels == 4);

    moos::u8 pixels[4];
    VkDeviceSize pixelsSize;

    if (isHdr) {
        pixelsSize = sizeof(vec4);
    } else {
        pixels[0] = (stbi_uc)(moos::clamp(pixel.x, 0.0f, 1.0f) * 255.99f);
        pixels[1] = (stbi_uc)(moos::clamp(pixel.y, 0.0f, 1.0f) * 255.99f);
        pixels[2] = (stbi_uc)(moos::clamp(pixel.z, 0.0f, 1.0f) * 255.99f);
        pixels[3] = (stbi_uc)(moos::clamp(pixel.w, 0.0f, 1.0f) * 255.99f);
        pixelsSize = 4 * sizeof(stbi_uc);
    }

    VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferCreateInfo.size = pixelsSize;

    VmaAllocationCreateInfo allocCreateInfo = {};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());

    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;
    if (vmaCreateBuffer(vulkanBackend.globalAllocator(), &bufferCreateInfo, &allocCreateInfo, &stagingBuffer, &stagingAllocation, nullptr) != VK_SUCCESS) {
        LogError("Could not create staging buffer for updating image with pixel-data.\n");
    }

    if (!vulkanBackend.setBufferMemoryUsingMapping(stagingAllocation, isHdr ? (uint8_t*)value_ptr(pixel) : (uint8_t*)pixels, pixelsSize)) {
        LogError("Could not set the buffer memory for the staging buffer for updating image with pixel-data.\n");
        return;
    }

    AtScopeExit cleanUpStagingBuffer([&]() {
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
        imageBarrier.subresourceRange.levelCount = 1;
        imageBarrier.subresourceRange.baseArrayLayer = 0;
        imageBarrier.subresourceRange.layerCount = 1;

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
            LogError("Could not transition the image to transfer optimal layout.\n");
            return;
        }
    }

    if (!vulkanBackend.copyBufferToImage(stagingBuffer, image, 1, 1, hasDepthFormat())) {
        LogError("Could not copy the staging buffer to the image.\n");
        return;
    }

    VkImageMemoryBarrier imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    {
        imageBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        imageBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        imageBarrier.image = image;
        imageBarrier.subresourceRange.aspectMask = aspectMask();
        imageBarrier.subresourceRange.baseMipLevel = 0;
        imageBarrier.subresourceRange.levelCount = 1;
        imageBarrier.subresourceRange.baseArrayLayer = 0;
        imageBarrier.subresourceRange.layerCount = 1;

        imageBarrier.srcAccessMask = 0;
        imageBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    }

    bool success = static_cast<VulkanBackend&>(backend()).issueSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
        vkCmdPipelineBarrier(commandBuffer,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
                             0, nullptr,
                             0, nullptr,
                             1, &imageBarrier);
    });

    if (!success) {
        LogError("Error transitioning layout after setting pixel data\n");
    }

    currentLayout = VK_IMAGE_LAYOUT_GENERAL;
}

void VulkanTexture::setData(const void* data, size_t size)
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
        LogError("VulkanBackend::updateTexture(): could not create staging buffer.\n");
    }

    if (!vulkanBackend.setBufferMemoryUsingMapping(stagingAllocation, (uint8_t*)data, size)) {
        LogError("VulkanBackend::updateTexture(): could set the buffer memory for the staging buffer.\n");
        return;
    }

    AtScopeExit cleanUpStagingBuffer([&]() {
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
        imageBarrier.subresourceRange.levelCount = 1;
        imageBarrier.subresourceRange.baseArrayLayer = 0;
        imageBarrier.subresourceRange.layerCount = 1;

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
            LogError("Could not transition the image to transfer optimal layout.\n");
            return;
        }
    }

    if (!vulkanBackend.copyBufferToImage(stagingBuffer, image, extent().width(), extent().height(), hasDepthFormat())) {
        LogError("Could not copy the staging buffer to the image.\n");
        return;
    }

    currentLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    if (mipmap() != Texture::Mipmap::None && extent().width() > 1 && extent().height() > 1) {
        generateMipmaps();
    } else {
        VkImageMemoryBarrier imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        {
            imageBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            imageBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

            imageBarrier.image = image;
            imageBarrier.subresourceRange.aspectMask = aspectMask();
            imageBarrier.subresourceRange.baseMipLevel = 0;
            imageBarrier.subresourceRange.levelCount = 1;
            imageBarrier.subresourceRange.baseArrayLayer = 0;
            imageBarrier.subresourceRange.layerCount = 1;

            imageBarrier.srcAccessMask = 0;
            imageBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        }

        bool success = static_cast<VulkanBackend&>(backend()).issueSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
            vkCmdPipelineBarrier(commandBuffer,
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
                                 0, nullptr,
                                 0, nullptr,
                                 1, &imageBarrier);
        });

        if (!success) {
            LogError("Error transitioning layout after setting texture data\n");
        }
    }
    currentLayout = VK_IMAGE_LAYOUT_GENERAL;
}

void VulkanTexture::generateMipmaps()
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    if (!hasMipmaps()) {
        LogError("VulkanTexture: generateMipmaps() called on texture which doesn't have space for mipmaps allocated. Ignoring request.\n");
        return;
    }

    if (currentLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
        LogError("VulkanTexture: generateMipmaps() called on texture which currently has the layout VK_IMAGE_LAYOUT_UNDEFINED. Ignoring request.\n");
        return;
    }

    VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.subresourceRange.aspectMask = aspectMask();
    barrier.image = image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    uint32_t levels = mipLevels();
    int32_t mipWidth = extent().width();
    int32_t mipHeight = extent().height();

    // We have to be very general in this function..
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkImageLayout finalLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkAccessFlags finalAccess = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

    bool success = static_cast<VulkanBackend&>(backend()).issueSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
        // Transition mips 1-n to transfer dst optimal
        {
            VkImageMemoryBarrier initialBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            initialBarrier.image = image;
            initialBarrier.subresourceRange.aspectMask = aspectMask();
            initialBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            initialBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            initialBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            initialBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            initialBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            initialBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            initialBarrier.subresourceRange.baseArrayLayer = 0;
            initialBarrier.subresourceRange.layerCount = 1;
            initialBarrier.subresourceRange.baseMipLevel = 1;
            initialBarrier.subresourceRange.levelCount = levels - 1;

            vkCmdPipelineBarrier(commandBuffer,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                                 0, nullptr,
                                 0, nullptr,
                                 1, &initialBarrier);
        }

        for (uint32_t i = 1; i < levels; ++i) {

            int32_t nextWidth = mipWidth > 1 ? mipWidth / 2 : 1;
            int32_t nextHeight = mipHeight > 1 ? mipHeight / 2 : 1;

            // The 'currentLayout' keeps track of the whole image (or kind of mip0) but when we are messing
            // with it here, it will have to be different for the different mip levels.
            VkImageLayout oldLayout = (i == 1) ? currentLayout : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

            barrier.subresourceRange.baseMipLevel = i - 1;
            barrier.oldLayout = oldLayout;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

            vkCmdPipelineBarrier(commandBuffer,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                                 0, nullptr,
                                 0, nullptr,
                                 1, &barrier);

            VkImageBlit blit = {};
            blit.srcOffsets[0] = { 0, 0, 0 };
            blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
            blit.srcSubresource.aspectMask = aspectMask();
            blit.srcSubresource.mipLevel = i - 1;
            blit.srcSubresource.baseArrayLayer = 0;
            blit.srcSubresource.layerCount = 1;
            blit.dstOffsets[0] = { 0, 0, 0 };
            blit.dstOffsets[1] = { nextWidth, nextHeight, 1 };
            blit.dstSubresource.aspectMask = aspectMask();
            blit.dstSubresource.mipLevel = i;
            blit.dstSubresource.baseArrayLayer = 0;
            blit.dstSubresource.layerCount = 1;

            vkCmdBlitImage(commandBuffer,
                           image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &blit,
                           VK_FILTER_LINEAR);

            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = finalLayout;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = finalAccess;

            vkCmdPipelineBarrier(commandBuffer,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, dstStage, 0,
                                 0, nullptr,
                                 0, nullptr,
                                 1, &barrier);

            mipWidth = nextWidth;
            mipHeight = nextHeight;
        }

        barrier.subresourceRange.baseMipLevel = levels - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = finalLayout;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = finalAccess;

        vkCmdPipelineBarrier(commandBuffer,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, dstStage, 0,
                             0, nullptr,
                             0, nullptr,
                             1, &barrier);
    });

    if (!success) {
        LogError("VulkanTexture: error while generating mipmaps\n");
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