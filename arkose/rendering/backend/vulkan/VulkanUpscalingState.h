#pragma once

#include "rendering/backend/base/UpscalingState.h"
#include "rendering/backend/vulkan/features/dlss/VulkanDLSS.h"

#if WITH_DLSS
#include <nvsdk_ngx_defs.h>
#endif

class VulkanUpscalingState : public UpscalingState {
public:
    VulkanUpscalingState(Backend&, UpscalingTech, UpscalingQuality, Extent2D renderRes, Extent2D outputRes);
    ~VulkanUpscalingState();

    virtual void setQuality(UpscalingQuality) override;

#if WITH_DLSS
    VkImageView velocityImageView {};
    NVSDK_NGX_Handle* dlssFeatureHandle { nullptr };
    void createDlssFeature();
#endif
};
