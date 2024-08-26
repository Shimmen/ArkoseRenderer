#include "VulkanUpscalingState.h"

#include "core/Assert.h"
#include "rendering/backend/vulkan/VulkanBackend.h"
#include "rendering/backend/vulkan/VulkanCommandList.h"

#if WITH_DLSS
#include <nvsdk_ngx_vk.h>
#endif

VulkanUpscalingState::VulkanUpscalingState(Backend& backend, UpscalingTech tech, UpscalingQuality quality, Extent2D renderRes, Extent2D outputRes)
    : UpscalingState(backend, tech, quality, renderRes, outputRes)
{
    switch (upscalingTech())
    {
    case UpscalingTech::None:
        ARKOSE_LOG(Fatal, "Creating upscaling state but with no upscaling tech");
        break;
#if WITH_DLSS
    case UpscalingTech::DLSS:
        createDlssFeature();
        break;
#endif
    default:
        NOT_YET_IMPLEMENTED();
    }
}

VulkanUpscalingState::~VulkanUpscalingState()
{
#if WITH_DLSS
    if (dlssFeatureHandle != nullptr) {
        NVSDK_NGX_Result destroyDlssFeatureResult = NVSDK_NGX_VULKAN_ReleaseFeature(dlssFeatureHandle);
        if (NVSDK_NGX_FAILED(destroyDlssFeatureResult)) {
            ARKOSE_LOG(Error, "Failed to destroy NVSDK NGX DLSS feature");
        }
    }
#endif
}

void VulkanUpscalingState::setQuality(UpscalingQuality quality)
{
    UpscalingState::setQuality(quality);

#if WITH_DLSS
    if (upscalingTech() == UpscalingTech::DLSS) {
        NOT_YET_IMPLEMENTED(); // not without rebuilding the render pipeline!
    }
#endif
}

#if WITH_DLSS
void VulkanUpscalingState::createDlssFeature()
{
    VulkanBackend& vulkanBackend = static_cast<VulkanBackend&>(backend());
    ARKOSE_ASSERT(vulkanBackend.hasDlssFeature()); // TODO: Handle error!
    VulkanDLSS& vulkanDlss = vulkanBackend.dlssFeature();

    UpscalingPreferences preferences = vulkanDlss.queryOptimalSettings(outputResolution(), quality());
    ARKOSE_ASSERT(preferences.preferredRenderResolution == renderResolution());
    m_optimalSharpness = preferences.preferredSharpening;

    constexpr bool inputIsHDR = true;
    dlssFeatureHandle = vulkanDlss.createWithSettings(renderResolution(), outputResolution(), quality(), inputIsHDR);
}
#endif
