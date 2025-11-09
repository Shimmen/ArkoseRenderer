#include "SSAONode.h"

#include "rendering/RenderPipeline.h"
#include <imgui.h>
#include <ark/random.h>

// Shader data
#include "shaders/shared/SSAOData.h"

void SSAONode::drawGui()
{
    ImGui::SliderFloat("Kernel radius (m)", &m_kernelRadius, 0.01f, 1.5f);
    ImGui::SliderFloat("Kernel exponent", &m_kernelExponent, 0.5f, 5.0f);

    // static bool applyBlur = true;
    // ImGui::Checkbox("Apply blur", &applyBlur);
}

RenderPipelineNode::ExecuteCallback SSAONode::construct(GpuScene& scene, Registry& reg)
{
    //
    // NOTE: We shouldn't rely on TAA to clean up the noise produced by this as the noise messes with history samples.
    // We should ensure we denoise it before we pass it on, and let TAA just smooth out the last little bit.
    //

    ///////////////////////
    // constructNode
    static constexpr int KernelSampleCount = 32;
    ARKOSE_ASSERT(KernelSampleCount <= SSAO_KERNEL_SAMPLE_MAX_COUNT);
    Buffer& kernelSampleBuffer = reg.createBuffer(generateKernel(KernelSampleCount), Buffer::Usage::ConstantBuffer);
    ///////////////////////

    // TODO: Handle resource modifications! For proper async handling
    Texture* sceneOpaqueDepth = reg.getTexture("SceneDepth");
    Texture* sceneOpaqueNormals = reg.getTexture("SceneNormalVelocity");

    Texture& ambientOcclusionTex = reg.createTexture2D(pipeline().renderResolution(), Texture::Format::R16F);
    reg.publish("AmbientOcclusion", ambientOcclusionTex);

    BindingSet& ssaoBindingSet = reg.createBindingSet({ ShaderBinding::storageTexture(ambientOcclusionTex, ShaderStage::Compute),
                                                        ShaderBinding::sampledTexture(*sceneOpaqueDepth, ShaderStage::Compute),
                                                        ShaderBinding::sampledTexture(*sceneOpaqueNormals, ShaderStage::Compute),
                                                        ShaderBinding::constantBuffer(*reg.getBuffer("SceneCameraData"), ShaderStage::Compute),
                                                        ShaderBinding::constantBuffer(kernelSampleBuffer, ShaderStage::Compute) });
    StateBindings ssaoStateBindings;
    ssaoStateBindings.at(0, ssaoBindingSet);

    ComputeState& ssaoComputeState = reg.createComputeState(Shader::createCompute("ssao/ssao.comp"), ssaoStateBindings);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        cmdList.setComputeState(ssaoComputeState);

        cmdList.setNamedUniform("targetSize", pipeline().renderResolution());
        cmdList.setNamedUniform("kernelRadius", m_kernelRadius);
        cmdList.setNamedUniform("kernelExponent", m_kernelExponent);
        cmdList.setNamedUniform("kernelSampleCount", KernelSampleCount);

        cmdList.dispatch({ ambientOcclusionTex.extent(), 1 }, { 32, 32, 1 });
        cmdList.textureWriteBarrier(ambientOcclusionTex);

        // TODO: If we don't blur we don't wanna have to make a copy..
        //if (applyBlur) {
        //    cmdList.setComputeState(blurComputeState);
        //    cmdList.dispatch({ resolution, 1 }, { 16, 16, 1 });
        //    cmdList.copyTexture(blurredAmbientOcclusionTex, ambientOcclusionTex, ImageFilter::Nearest);
        //    cmdList.textureWriteBarrier(blurredAmbientOcclusionTex);
        //}

    };
}

std::vector<vec4> SSAONode::generateKernel(int numSamples) const
{
    std::vector<vec4> kernelSamples {};
    kernelSamples.reserve(numSamples);

    ark::Random rng {};
    for (int i = 0; i < numSamples; ++i) {

        // Places samples somewhat randomly in the xy-hemisphere but ensures they appropriately cover the entire radius,
        // with greater density towards the center. I'm sure this can be tweaked a lot.
        vec3 hemisphereSample = vec3(rng.randomFloatInRange(-1.0f, +1.0f),
                                     rng.randomFloatInRange(-1.0f, +1.0f),
                                     rng.randomFloatInRange(0.0f, +1.0f));
        float sampleScale = float(i) / float(numSamples - 1);
        sampleScale = ark::lerp(0.1f, 1.0f, sampleScale * sampleScale);
        vec3 sample = sampleScale * normalize(hemisphereSample);

        kernelSamples.push_back(vec4(sample, 0.0f));
    }

    return kernelSamples;
}
