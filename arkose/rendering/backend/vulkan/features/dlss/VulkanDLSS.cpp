#include "VulkanDLSS.h"

#if WITH_DLSS

#include "core/Types.h"
#include "rendering/backend/vulkan/VulkanBackend.h"
#include "rendering/backend/vulkan/VulkanTexture.h"

#include <nvsdk_ngx.h>
#include <nvsdk_ngx_vk.h>
#include <nvsdk_ngx_helpers.h>
#include <nvsdk_ngx_helpers_vk.h>
#include <nvsdk_ngx_defs.h>
#include <nvsdk_ngx_params.h>

VulkanDLSS::VulkanDLSS(VulkanBackend& backend, VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device)
    : m_backend(backend)
    , m_instance(instance)
    , m_physicalDevice(physicalDevice)
    , m_device(device)
{
    SCOPED_PROFILE_ZONE_BACKEND();

    NVSDK_NGX_Result result = NVSDK_NGX_UpdateFeature(&applicationIdentifier(), NVSDK_NGX_Feature_ImageSuperResolution);
    if (result != NVSDK_NGX_Result::NVSDK_NGX_Result_Success) {
        ARKOSE_LOG(Info, "Failed to update NVSDK NGX DLSS3 feature");
    }

    NVSDK_NGX_Result initResult = NVSDK_NGX_VULKAN_Init(applicationIdentifier().v.ApplicationId, applicationDataPath(), instance, physicalDevice, device);
    if (NVSDK_NGX_FAILED(initResult)) {
        ARKOSE_LOG(Fatal, "Failed to initialize NVSDK NGX");
    }

    NVSDK_NGX_Result getCapParamsResult = NVSDK_NGX_VULKAN_GetCapabilityParameters(&m_ngxParameters);
    if (NVSDK_NGX_FAILED(getCapParamsResult)) {
        ARKOSE_LOG(Fatal, "Failed to get NVSDK NGX capability parameters");
    }

    int dlssAvailable = 0;
    NVSDK_NGX_Result dlssCheckSupportResult = m_ngxParameters->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &dlssAvailable);
    if (NVSDK_NGX_FAILED(dlssCheckSupportResult)) {
        ARKOSE_LOG(Fatal, "Failed to check NVSDK NGX DLSS support");
    }

    m_dlssSupported = dlssAvailable != 0;
}

VulkanDLSS::~VulkanDLSS()
{
    for (auto [sourceTexture, remappedImageView] : m_customRemappedImageViews) {
        vkDestroyImageView(m_device, remappedImageView, nullptr);
    }

    NVSDK_NGX_Result destroyParamsResult = NVSDK_NGX_VULKAN_DestroyParameters(m_ngxParameters);
    if (NVSDK_NGX_FAILED(destroyParamsResult)) {
        ARKOSE_LOG(Error, "Failed to destroy NVSDK NGX parameters object");
    }

    NVSDK_NGX_Result shutdownResult = NVSDK_NGX_VULKAN_Shutdown1(m_device);
    if (NVSDK_NGX_FAILED(shutdownResult)) {
        ARKOSE_LOG(Error, "Failed to shutdown NVSDK NGX");
    }
}

NVSDK_NGX_PerfQuality_Value VulkanDLSS::dlssQualityForUpscalingQuality(UpscalingQuality quality)
{
    switch (quality) {
    case UpscalingQuality::NativeResolution:
        return NVSDK_NGX_PerfQuality_Value_DLAA;
    case UpscalingQuality::BestQuality:
        return NVSDK_NGX_PerfQuality_Value_UltraQuality;
    case UpscalingQuality::GoodQuality:
        return NVSDK_NGX_PerfQuality_Value_MaxQuality;
    case UpscalingQuality::Balanced:
        return NVSDK_NGX_PerfQuality_Value_Balanced;
    case UpscalingQuality::GoodPerformance:
        return NVSDK_NGX_PerfQuality_Value_MaxPerf;
    case UpscalingQuality::BestPerformance:
        return NVSDK_NGX_PerfQuality_Value_UltraPerformance;
    default:
        ASSERT_NOT_REACHED();
    }
}

UpscalingPreferences VulkanDLSS::queryOptimalSettings(Extent2D targetResolution, UpscalingQuality quality)
{
    UpscalingPreferences preferences {};

    u32 optimalRenderWidth, optimalRenderHeight;
    float recommendedSharpness;

    u32 minRenderWidth, minRenderHeight;
    u32 maxRenderWidth, maxRenderHeight;

    NVSDK_NGX_PerfQuality_Value dlssQuality = dlssQualityForUpscalingQuality(quality);
    NVSDK_NGX_Result result = NGX_DLSS_GET_OPTIMAL_SETTINGS(m_ngxParameters, targetResolution.width(), targetResolution.height(), dlssQuality,
                                                            &optimalRenderWidth, &optimalRenderHeight,
                                                            &maxRenderWidth, &maxRenderHeight,
                                                            &minRenderWidth, &minRenderHeight,
                                                            &recommendedSharpness);

    if (NVSDK_NGX_FAILED(result) || optimalRenderWidth == 0 || optimalRenderHeight == 0) {
        ARKOSE_LOG(Error, "Failed to get optimal DLSS settings");
        optimalRenderWidth = targetResolution.width();
        optimalRenderHeight = targetResolution.height();
        recommendedSharpness = 0.0f;
    }

    // DLSS sharpening is deprecated & disabled in the API
    recommendedSharpness = 0.0f;

    return UpscalingPreferences { .preferredRenderResolution = { optimalRenderWidth, optimalRenderHeight },
                                  .preferredSharpening = recommendedSharpness };
}

NVSDK_NGX_Handle* VulkanDLSS::createWithSettings(Extent2D renderResolution, Extent2D targetResolution, UpscalingQuality quality, bool inputIsHDR)
{
    int dlssCreateFeatureFlags = NVSDK_NGX_DLSS_Feature_Flags_None;

    // From DLSS Programming Guide:
    // Motion vectors are typically calculated at the same resolution as the input color frame (i.e. at the render resolution).
    // If the rendering engine supports calculating motion vectors at the display / output resolution and dilating the motion
    // vectors, DLSS can accept those by setting the flag to "0". This is preferred, though uncommon, and can result in higher
    // quality antialiasing of moving objects and less blurring of small objects and thin details. For clarity, if standard
    // input resolution motion vectors are sent they do not need to be dilated, DLSS dilates them internally. If display
    // resolution motion vectors are sent, they must be dilated.
    dlssCreateFeatureFlags |= NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;

    // From DLSS Programming Guide:
    // Set this flag to "1" when the motion vectors do include sub-pixel jitter. DLSS then internally subtracts jitter from the
    // motion vectors using the jitter offset values that are provided during the �Evaluate� call.When set to "0", DLSS uses the
    // motion vectors directly without any adjustment.
    constexpr bool motionVectorsAreJittered = false; // NOTE: we un-jitter when we calculate velocity!
    dlssCreateFeatureFlags |= motionVectorsAreJittered ? NVSDK_NGX_DLSS_Feature_Flags_MVJittered : 0;

    dlssCreateFeatureFlags |= inputIsHDR ? NVSDK_NGX_DLSS_Feature_Flags_IsHDR : 0;

    // We don't use reverse z, for now.
    //dlssCreateFeatureFlags |= depthInverted ? NVSDK_NGX_DLSS_Feature_Flags_DepthInverted : 0;

    // DLSS sharpening is deprecated and removed from their API
    //dlssCreateFeatureFlags |= enableSharpening ? NVSDK_NGX_DLSS_Feature_Flags_DoSharpening : 0;

    // We don't use auto-exposure, for now.
    //dlssCreateFeatureFlags |= enableAutoExposure ? NVSDK_NGX_DLSS_Feature_Flags_AutoExposure : 0;

    NVSDK_NGX_DLSS_Create_Params dlssCreateParams {};
    dlssCreateParams.InFeatureCreateFlags = dlssCreateFeatureFlags;
    dlssCreateParams.Feature.InWidth = renderResolution.width();
    dlssCreateParams.Feature.InHeight = renderResolution.height();
    dlssCreateParams.Feature.InTargetWidth = targetResolution.width();
    dlssCreateParams.Feature.InTargetHeight = targetResolution.height();
    dlssCreateParams.Feature.InPerfQualityValue = dlssQualityForUpscalingQuality(quality);

    NVSDK_NGX_Handle* dlssFeatureHandle;

    bool creationSuccess = true;
    bool issueCommandSuccess = m_backend.issueSingleTimeCommand([this, &creationSuccess, &dlssCreateParams, &dlssFeatureHandle](VkCommandBuffer commandBuffer) {

        // TODO: What do these mean?!
        constexpr u32 CreationNodeMask = 1;
        constexpr u32 VisibilityNodeMask = 1;

        NVSDK_NGX_Result createDlssResult = NGX_VULKAN_CREATE_DLSS_EXT1(m_device, commandBuffer,
                                                                        CreationNodeMask, VisibilityNodeMask,
                                                                        &dlssFeatureHandle, m_ngxParameters,
                                                                        &dlssCreateParams);

        if (NVSDK_NGX_FAILED(createDlssResult)) {
            ARKOSE_LOG(Error, "Failed to create NVSDK NGX DLSS feature");
            creationSuccess = false;
        }
    });

    if (!issueCommandSuccess) {
        ARKOSE_LOG(Error, "Failed to issue commands for creating NVSDK NGX DLSS feature");
    }

    if (creationSuccess && issueCommandSuccess) {
        return dlssFeatureHandle;
    } else {
        return nullptr;
    }
}

bool VulkanDLSS::evaluate(VkCommandBuffer commandBuffer, NVSDK_NGX_Handle* dlssFeatureHandle, UpscalingParameters const& parameters)
{
    // Ensure the upscaled texture is in the expected image layout for DLSS
    VulkanTexture& upscaledTexture = static_cast<VulkanTexture&>(*parameters.upscaledColor);
    constexpr VkImageLayout upscaledTextureTargetLayout = VK_IMAGE_LAYOUT_GENERAL;
    if (upscaledTexture.currentLayout != upscaledTextureTargetLayout) {

        VkImageMemoryBarrier imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        imageBarrier.oldLayout = upscaledTexture.currentLayout;
        imageBarrier.newLayout = upscaledTextureTargetLayout;
        imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        ARKOSE_ASSERT(upscaledTexture.mipLevels() == 1);
        ARKOSE_ASSERT(upscaledTexture.layerCount() == 1);

        imageBarrier.image = upscaledTexture.image;
        imageBarrier.subresourceRange.aspectMask = upscaledTexture.aspectMask();
        imageBarrier.subresourceRange.baseMipLevel = 0;
        imageBarrier.subresourceRange.levelCount = 1;
        imageBarrier.subresourceRange.baseArrayLayer = 0;
        imageBarrier.subresourceRange.layerCount = 1;

        VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        imageBarrier.srcAccessMask = 0;

        VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;

        vkCmdPipelineBarrier(commandBuffer,
                             sourceStage, destinationStage, 0,
                             0, nullptr,
                             0, nullptr,
                             1, &imageBarrier);
        upscaledTexture.currentLayout = upscaledTextureTargetLayout;
    }

    auto textureToNgxResourceVk = [this](Texture const& texture, bool writeCapable, std::optional<VkComponentMapping> componentMapping) {
        auto const& vulkanTexture = static_cast<VulkanTexture const&>(texture);

        VkImageView imageView = vulkanTexture.imageView;
        if (componentMapping.has_value()) {
            // NOTE: This assumes that we'd never try to use more than one component mapping
            // per texture, which seems like a pretty safe assumption to make.
            auto entry = m_customRemappedImageViews.find(&vulkanTexture);
            if (entry == m_customRemappedImageViews.end()) {
                VkImageView remappedImageView = vulkanTexture.createImageView(0, 1, componentMapping.value());
                m_customRemappedImageViews[&vulkanTexture] = remappedImageView;
                imageView = remappedImageView;
            } else {
                imageView = entry->second;
            }
        }

        VkImageSubresourceRange subresourceRange {};
        subresourceRange.aspectMask = vulkanTexture.aspectMask();
        subresourceRange.baseMipLevel = 0;
        subresourceRange.levelCount = vulkanTexture.mipLevels();
        subresourceRange.baseArrayLayer = 0;
        subresourceRange.layerCount = 1;

        return NVSDK_NGX_Create_ImageView_Resource_VK(imageView, vulkanTexture.image, subresourceRange, vulkanTexture.vkFormat,
                                                      vulkanTexture.extent().width(), vulkanTexture.extent().height(), writeCapable);
    };

    NVSDK_NGX_Resource_VK dstColorResource = textureToNgxResourceVk(*parameters.upscaledColor, true, {});

    NVSDK_NGX_Resource_VK srcColorResource = textureToNgxResourceVk(*parameters.inputColor, false, {});
    NVSDK_NGX_Resource_VK depthResource = textureToNgxResourceVk(*parameters.depthTexture, false, {});

    std::optional<VkComponentMapping> velocityComponentMapping = std::nullopt;
    if (parameters.velocityTextureIsSceneNormalVelocity) {
        // NOTE: Arkose's default "SceneNormalVelocity" puts the velocity in the B and A components but DLSS expects in in R and G
        velocityComponentMapping = { VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A, VK_COMPONENT_SWIZZLE_ZERO, VK_COMPONENT_SWIZZLE_ZERO };
    }

    NVSDK_NGX_Resource_VK motionVectorsResource = textureToNgxResourceVk(*parameters.velocityTexture, false, velocityComponentMapping);

    NVSDK_NGX_VK_DLSS_Eval_Params dlssEvalParams {};

    // Required parameters
    dlssEvalParams.Feature.pInColor = &srcColorResource;
    dlssEvalParams.Feature.pInOutput = &dstColorResource;
    dlssEvalParams.Feature.InSharpness = parameters.sharpness; // optional
    dlssEvalParams.pInDepth = &depthResource;
    dlssEvalParams.pInMotionVectors = &motionVectorsResource;
    dlssEvalParams.InJitterOffsetX = -parameters.frustumJitterOffset.x;
    dlssEvalParams.InJitterOffsetY = -parameters.frustumJitterOffset.y;
    dlssEvalParams.InRenderSubrectDimensions.Width = parameters.inputColor->extent().width();
    dlssEvalParams.InRenderSubrectDimensions.Height = parameters.inputColor->extent().height();

    // Optional parameters

    dlssEvalParams.InReset = parameters.resetAccumulation;

    // Motion vector scale
    if (parameters.velocityTextureIsSceneNormalVelocity) {
        // NOTE: Arkose's default "SceneNormalVelocity" motion vectors typically point point towards the direction of motion, but DLSS expects it to point towards prev. frame
        // NOTE: Arkose's default "SceneNormalVelocity" motion vectors are in uv-space but DLSS expects them to be in pixel space.
        dlssEvalParams.InMVScaleX = -1.0f * srcColorResource.Resource.ImageViewInfo.Width;
        dlssEvalParams.InMVScaleY = -1.0f * srcColorResource.Resource.ImageViewInfo.Height;
    }

    if (parameters.exposureTexture != nullptr) {
        // I would guess for auto exposure, so we don't need to do any readback?
        NVSDK_NGX_Resource_VK exposureResource = textureToNgxResourceVk(*parameters.exposureTexture, false, {});
        dlssEvalParams.pInExposureTexture = &exposureResource;
    }

    dlssEvalParams.pInBiasCurrentColorMask = nullptr; // TODO: What is this?

    // TODO: Figure this out.. pre-exposure of 1.0 clearly look best, but it should be our correct pre-exposure?!
    dlssEvalParams.InPreExposure = 1.0f;// parameters.preExposure;
    dlssEvalParams.InExposureScale = 1.0f; // TODO: What is this?

    dlssEvalParams.InIndicatorInvertXAxis = 0;
    dlssEvalParams.InIndicatorInvertYAxis = 0;

    NVSDK_NGX_Result evaluateResult = NGX_VULKAN_EVALUATE_DLSS_EXT(commandBuffer, dlssFeatureHandle, m_ngxParameters, &dlssEvalParams);

    if (NVSDK_NGX_FAILED(evaluateResult)) {
        ARKOSE_LOG(Fatal, "Failed to evaluate DLSS, exiting.");
    }

    return true;
}

std::vector<VkExtensionProperties*> VulkanDLSS::requiredInstanceExtensions()
{
    NVSDK_NGX_FeatureDiscoveryInfo info {};
    info.SDKVersion = NVSDK_NGX_Version_API;
    info.FeatureID = NVSDK_NGX_Feature_SuperSampling;
    info.Identifier = applicationIdentifier();
    info.ApplicationDataPath = applicationDataPath();
    info.FeatureInfo = nullptr;

    NVSDK_NGX_Result result;

    u32 extensionCount;
    VkExtensionProperties* extensions;
    result = NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(&info, &extensionCount, &extensions);
    if (NVSDK_NGX_FAILED(result)) {
        ARKOSE_LOG(Error, "Failed to get feature instance extension requirements for NVSDK NGX");
    }

    std::vector<VkExtensionProperties*> requiredExtensions(extensionCount);
    result = NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(&info, &extensionCount, requiredExtensions.data());
    if (NVSDK_NGX_FAILED(result)) {
        ARKOSE_LOG(Error, "Failed to get feature instance extension requirements for NVSDK NGX");
    }

    return requiredExtensions;
}

std::vector<VkExtensionProperties*> VulkanDLSS::requiredDeviceExtensions(VkInstance instance, VkPhysicalDevice physicalDevice)
{
    NVSDK_NGX_FeatureDiscoveryInfo info {};
    info.SDKVersion = NVSDK_NGX_Version_API;
    info.FeatureID = NVSDK_NGX_Feature_SuperSampling;
    info.Identifier = applicationIdentifier();
    info.ApplicationDataPath = applicationDataPath();
    info.FeatureInfo = nullptr;

    NVSDK_NGX_Result result;

    u32 extensionCount = 0;
    VkExtensionProperties* extensions;
    result = NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(instance, physicalDevice, &info, &extensionCount, &extensions);
    if (NVSDK_NGX_FAILED(result)) {
        ARKOSE_LOG(Fatal, "Failed to get feature device extension requirements for DLSS3");
    }

    std::vector<VkExtensionProperties*> requiredExtensions;
    for (u32 i = 0; i < extensionCount; ++i) {
        requiredExtensions.push_back(&extensions[i]);
    }

    return requiredExtensions;
}

NVSDK_NGX_Application_Identifier const& VulkanDLSS::applicationIdentifier()
{
    static NVSDK_NGX_Application_Identifier applicationId {};
    applicationId.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Application_Id;
    applicationId.v.ProjectDesc.EngineType = NVSDK_NGX_ENGINE_TYPE_CUSTOM;
    applicationId.v.ProjectDesc.ProjectId = "Arkose";
    applicationId.v.ProjectDesc.EngineVersion = "1.0.0";
    return applicationId;
}

wchar_t const* VulkanDLSS::applicationDataPath()
{
    FileIO::ensureDirectory("logs/");
    static wchar_t const* path = L"logs";
    return path;
}

VulkanDLSSExternalFeature::VulkanDLSSExternalFeature(Backend& backend, ExternalFeatureCreateParamsDLSS const& params)
    : ExternalFeature(backend, ExternalFeatureType::DLSS)
{
    VulkanBackend& vulkanBackend = static_cast<VulkanBackend&>(backend);
    ARKOSE_ASSERT(vulkanBackend.hasDlssFeature()); // TODO: Handle error!
    VulkanDLSS& vulkanDlss = vulkanBackend.dlssFeature();

    UpscalingPreferences preferences = vulkanDlss.queryOptimalSettings(params.outputResolution, params.quality);
    ARKOSE_ASSERT(preferences.preferredRenderResolution == params.renderResolution);
    m_optimalSharpness = preferences.preferredSharpening;

    float renderResolutionX = static_cast<float>(params.renderResolution.width());
    float outputResolutionX = static_cast<float>(params.outputResolution.width());
    m_optimalMipBias = std::log2(renderResolutionX / outputResolutionX) - 1.0f;

    constexpr bool inputIsHDR = true;
    dlssFeatureHandle = vulkanDlss.createWithSettings(params.renderResolution, params.outputResolution, params.quality, inputIsHDR);
}

float VulkanDLSSExternalFeature::queryParameterF(ExternalFeatureParameter param)
{
    switch (param) {
    case ExternalFeatureParameter::DLSS_OptimalMipBias:
        return m_optimalMipBias;
    case ExternalFeatureParameter::DLSS_OptimalSharpness:
        return m_optimalSharpness;
    }

    return ExternalFeature::queryParameterF(param);
}

#endif // WITH_DLSS
