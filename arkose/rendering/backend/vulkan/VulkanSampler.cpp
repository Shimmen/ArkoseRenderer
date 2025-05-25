#include "VulkanSampler.h"

#include "rendering/backend/vulkan/VulkanBackend.h"
#include "utility/Profiling.h"

VulkanSampler::VulkanSampler(Backend& backend, Description& desc)
    : Sampler(backend, desc)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend);

    VkSamplerCreateInfo samplerCreateInfo = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };

    samplerCreateInfo.flags = 0u;

    switch (desc.magFilter) {
    case ImageFilter::Linear:
        samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
        break;
    case ImageFilter::Nearest:
        samplerCreateInfo.magFilter = VK_FILTER_NEAREST;
        break;
    }

    switch (desc.minFilter) {
    case ImageFilter::Linear:
        samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
        break;
    case ImageFilter::Nearest:
        samplerCreateInfo.minFilter = VK_FILTER_NEAREST;
        break;
    }

    switch (desc.mipmap) {
    case Mipmap::None:
        samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        break;
    case Mipmap::Nearest:
        samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        break;
    case Mipmap::Linear:
        samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        break;
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

    samplerCreateInfo.addressModeU = wrapModeToAddressMode(desc.wrapMode.u);
    samplerCreateInfo.addressModeV = wrapModeToAddressMode(desc.wrapMode.v);
    samplerCreateInfo.addressModeW = wrapModeToAddressMode(desc.wrapMode.w);

    samplerCreateInfo.mipLodBias = 0.0f;

    samplerCreateInfo.anisotropyEnable = VK_TRUE;
    samplerCreateInfo.maxAnisotropy = 16.0f;

    samplerCreateInfo.compareEnable = VK_FALSE;
    samplerCreateInfo.compareOp = VK_COMPARE_OP_ALWAYS;

    samplerCreateInfo.minLod = 0.0f;
    samplerCreateInfo.maxLod = VK_LOD_CLAMP_NONE;

    samplerCreateInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;

    samplerCreateInfo.unnormalizedCoordinates = VK_FALSE;

    if (vkCreateSampler(vulkanBackend.device(), &samplerCreateInfo, nullptr, &sampler) != VK_SUCCESS) {
        ARKOSE_LOG(Error, "VulkanSampler: failed to create sampler.");
    }
}

VulkanSampler::~VulkanSampler()
{
    if (!hasBackend()) {
        return;
    }
    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());
    vkDestroySampler(vulkanBackend.device(), sampler, nullptr);
}

void VulkanSampler::setName(std::string const& name)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    Resource::setName(name);

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());
    if (vulkanBackend.hasDebugUtilsSupport()) {

        VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
        nameInfo.objectType = VK_OBJECT_TYPE_SAMPLER;
        nameInfo.objectHandle = reinterpret_cast<uint64_t>(sampler);
        nameInfo.pObjectName = name.c_str();

        if (vulkanBackend.debugUtils().vkSetDebugUtilsObjectNameEXT(vulkanBackend.device(), &nameInfo) != VK_SUCCESS) {
            ARKOSE_LOG(Warning, "Could not set debug name for vulkan sampler resource.");
        }
    }
}
