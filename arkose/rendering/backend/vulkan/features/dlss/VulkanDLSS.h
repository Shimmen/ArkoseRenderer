#pragma once

#if WITH_DLSS

#include "utility/Extent.h"
#include "rendering/UpscalingParameters.h"
#include "rendering/backend/base/ExternalFeature.h"
#include <ark/vector.h>
#include <vector>
#include <vulkan/vulkan.h>

#include <nvsdk_ngx_defs.h>
#include <nvsdk_ngx_params.h>

class VulkanBackend;
struct VulkanTexture;

class VulkanDLSS {
public:
    VulkanDLSS(VulkanBackend&, VkInstance, VkPhysicalDevice, VkDevice);
    ~VulkanDLSS();

    // Is DLSS supported and the feature is ready to use?
    bool isReadyToUse() const { return m_dlssSupported; }

    UpscalingPreferences queryOptimalSettings(Extent2D targetResolution, UpscalingQuality);
    NVSDK_NGX_Handle* createWithSettings(Extent2D inputResolution, Extent2D outputResolution, UpscalingQuality, bool inputIsHDR);
    bool evaluate(VkCommandBuffer, NVSDK_NGX_Handle* dlssFeatureHandle, UpscalingParameters const&);

    static NVSDK_NGX_PerfQuality_Value dlssQualityForUpscalingQuality(UpscalingQuality);

    static std::vector<VkExtensionProperties*> requiredInstanceExtensions();
    static std::vector<VkExtensionProperties*> requiredDeviceExtensions(VkInstance, VkPhysicalDevice);

    static NVSDK_NGX_Application_Identifier const& applicationIdentifier();
    static wchar_t const* applicationDataPath();

private:
    VulkanBackend& m_backend;
    VkInstance m_instance;
    VkPhysicalDevice m_physicalDevice;
    VkDevice m_device;

    bool m_dlssSupported { false };

    NVSDK_NGX_Parameter* m_ngxParameters { nullptr };

    std::unordered_map<VulkanTexture const*, VkImageView> m_customRemappedImageViews {};

};

class VulkanDLSSExternalFeature final : public ExternalFeature {
public:
    VulkanDLSSExternalFeature(Backend&, ExternalFeatureCreateParamsDLSS const&);

    float queryParameterF(ExternalFeatureParameter) override;

    float m_optimalSharpness { 0.0f };
    float m_optimalMipBias { 0.0f };
    NVSDK_NGX_Handle* dlssFeatureHandle { nullptr };
};

#endif // WITH_DLSS
