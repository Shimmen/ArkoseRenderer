#include "VulkanNRD.h"

#if WITH_NRD

#include "core/Types.h"
#include "core/Logging.h"
#include "rendering/backend/vulkan/VulkanBackend.h"
#include "rendering/backend/shader/ShaderManager.h"
#include "scene/camera/Camera.h"

static constexpr u32 NRDDenoiserId_SigmaShadow = 1;

VulkanNRD::VulkanNRD(VulkanBackend& backend)
    : m_backend(backend)
{
    std::array<nrd::DenoiserDesc, 1> denoiserDescs = {
        { NRDDenoiserId_SigmaShadow, nrd::Denoiser::SIGMA_SHADOW }
    };

    nrd::InstanceCreationDesc instanceCreationDesc = {};
    instanceCreationDesc.allocationCallbacks = {};
    instanceCreationDesc.denoisers = denoiserDescs.data();
    instanceCreationDesc.denoisersNum = narrow_cast<u32>(denoiserDescs.size());

    if (nrd::CreateInstance(instanceCreationDesc, m_nrdInstance) != nrd::Result::SUCCESS) {
        ARKOSE_LOG(Error, "Failed to create NRD instance");
    }

    nrd::InstanceDesc const& instanceDesc = nrd::GetInstanceDesc(*m_nrdInstance);

    // Register all NRD shaders upfront
    for (u32 idx = 0; idx < instanceDesc.pipelinesNum; idx++) {
        nrd::PipelineDesc const& nrdPipelineDesc = instanceDesc.pipelines[idx];

        nrd::ComputeShaderDesc const& nrdComputeShader = nrdPipelineDesc.computeShaderSPIRV;
        ARKOSE_ASSERT(strcmp(nrdPipelineDesc.shaderEntryPointName, "main") == 0);

        u32 const* spirvStream = reinterpret_cast<u32 const*>(nrdComputeShader.bytecode);
        ShaderManager::SpirvData spirvData { spirvStream, spirvStream + nrdComputeShader.size / sizeof(u32) };

        //ShaderManager::instance().registerPrecompiledShaderFile(nrdPipelineDesc.shaderFileName, spirvData);
    }
}

VulkanNRD::~VulkanNRD()
{
    if (m_nrdInstance) {
        nrd::DestroyInstance(*m_nrdInstance);
    }
}

bool VulkanNRD::isReadyToUse() const
{
    return m_nrdInstance != nullptr;
}

VulkanNRDSigmaShadowExternalFeature::VulkanNRDSigmaShadowExternalFeature(VulkanBackend& backend, VulkanNRD& nrd, ExternalFeatureCreateParamsNRDSigmaShadow const& params)
    : ExternalFeature(backend, ExternalFeatureType::NRD_SigmaShadow)
    , m_nrd(nrd)
{
    // Note: NRD instance is created in VulkanNRD constructor
}

VulkanNRDSigmaShadowExternalFeature::~VulkanNRDSigmaShadowExternalFeature()
{
}

void VulkanNRDSigmaShadowExternalFeature::evaluate(ExternalFeatureEvaluateParamsNRDSigmaShadow const& params) const
{
    nrd::Instance& nrdInstance = *m_nrd.nrdInstance();

    nrd::CommonSettings commonSettings = {};

    auto matrixToFloat16Array = [](float outMatrix[16], mat4 inMatrix) {
        for (u32 i = 0; i < 16; i++) {
            outMatrix[i] = inMatrix[i / 4][i % 4];
        }
    };

    // TODO: Should be unjittered!!
    matrixToFloat16Array(commonSettings.viewToClipMatrix, params.mainCamera->projectionMatrix());
    matrixToFloat16Array(commonSettings.viewToClipMatrixPrev, params.mainCamera->previousFrameProjectionMatrix());
    matrixToFloat16Array(commonSettings.worldToViewMatrix, params.mainCamera->viewMatrix());
    matrixToFloat16Array(commonSettings.worldToViewMatrixPrev, params.mainCamera->previousFrameViewMatrix());

    u16 resourceSize[2] = { narrow_cast<u16>(params.inputShadowMask->extent().width()),
                            narrow_cast<u16>(params.inputShadowMask->extent().height()) };

    // Used as "mv = IN_MV * motionVectorScale" (use .z = 0 for 2D screen-space motion)
    // Expected usage: "pixelUvPrev = pixelUv + mv.xy" (where "pixelUv" is in (0; 1) range)
    // NOTE: Arkose's default "SceneNormalVelocity" motion vectors typically point point towards the direction of motion, but NRD expects it to point towards prev. frame
    // NOTE: Arkose's default "SceneNormalVelocity" motion vectors are in uv-space but NRD expects them to be in pixel space.
    commonSettings.motionVectorScale[0] = -1.0f * resourceSize[0];
    commonSettings.motionVectorScale[1] = -1.0f * resourceSize[1];
    commonSettings.motionVectorScale[2] = 0.0f;

    // [-0.5; 0.5] - sampleUv = pixelUv + cameraJitter
    commonSettings.cameraJitter[0] = params.mainCamera->frustumJitterPixelOffset().x;
    commonSettings.cameraJitter[1] = params.mainCamera->frustumJitterPixelOffset().y;
    commonSettings.cameraJitterPrev[0] = params.mainCamera->previousFrameFrustumJitterPixelOffset().x;
    commonSettings.cameraJitterPrev[1] = params.mainCamera->previousFrameFrustumJitterPixelOffset().y;

    commonSettings.resourceSize[0] = resourceSize[0];
    commonSettings.resourceSize[1] = resourceSize[1];
    commonSettings.resourceSizePrev[0] = resourceSize[0];
    commonSettings.resourceSizePrev[1] = resourceSize[1];

    commonSettings.rectSize[0] = resourceSize[0];
    commonSettings.rectSize[1] = resourceSize[1];
    commonSettings.rectSizePrev[0] = resourceSize[0];
    commonSettings.rectSizePrev[1] = resourceSize[1];

    commonSettings.frameIndex = params.frameIndex;
    commonSettings.accumulationMode = params.resetAccumulation
        ? nrd::AccumulationMode::CLEAR_AND_RESTART
        : nrd::AccumulationMode::CONTINUE;

    nrd::Result result = nrd::SetCommonSettings(nrdInstance, commonSettings);
    if (result != nrd::Result::SUCCESS) {
        ARKOSE_LOG(Error, "Failed to set NRD common settings");
        return;
    }

    nrd::SigmaSettings sigmaSettings {};
    if (nrd::SetDenoiserSettings(nrdInstance, NRDDenoiserId_SigmaShadow, &sigmaSettings) != nrd::Result::SUCCESS) {
        ARKOSE_LOG(Error, "Failed to set NRD denoiser settings for the SigmaShadow denoiser");
        return;
    }

    u32 numDispatchDescs = 0;
    const nrd::DispatchDesc* dispatchDescs;
    if (nrd::GetComputeDispatches(nrdInstance, &NRDDenoiserId_SigmaShadow, 1, dispatchDescs, numDispatchDescs) != nrd::Result::SUCCESS) {
        ARKOSE_LOG(Error, "Failed to get NRD compute dispatch descriptors for the SigmaShadow denoiser");
        return;
    }

    for (u32 dispatchIdx = 0; dispatchIdx < numDispatchDescs; dispatchIdx++) {
        nrd::DispatchDesc const& dispatchDesc = dispatchDescs[dispatchIdx];

        // TODO: Dispatch!
        (void)dispatchDesc;
    }
}

#endif // WITH_NRD
