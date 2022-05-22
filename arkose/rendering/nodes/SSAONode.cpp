#include "SSAONode.h"

#include <imgui.h>
#include <moos/random.h>

// Shader data
#include "SSAOData.h"

RenderPipelineNode::ExecuteCallback SSAONode::construct(GpuScene& scene, Registry& reg)
{
    ///////////////////////
    // constructNode
    static constexpr int KernelSampleCount = 32;
    ARKOSE_ASSERT(KernelSampleCount <= SSAO_KERNEL_SAMPLE_MAX_COUNT);
    Buffer& kernelSampleBuffer = reg.createBuffer(generateKernel(KernelSampleCount), Buffer::Usage::ConstantBuffer, Buffer::MemoryHint::GpuOptimal);
    ///////////////////////

    // TODO: Handle resource modifications! For proper async handling
    Texture* sceneOpaqueDepth = reg.getTexture("SceneDepth");
    Texture* sceneOpaqueNormals = reg.getTexture("SceneNormalVelocity");

    Texture& ambientOcclusionTex = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA16F); //Texture::Format::R16F);
    reg.publish("AmbientOcclusion", ambientOcclusionTex);

    BindingSet& ssaoBindingSet = reg.createBindingSet({ { 0, ShaderStage::Compute, &ambientOcclusionTex, ShaderBindingType::StorageTexture },
                                                        { 1, ShaderStage::Compute, sceneOpaqueDepth, ShaderBindingType::TextureSampler },
                                                        { 2, ShaderStage::Compute, sceneOpaqueNormals, ShaderBindingType::TextureSampler },
                                                        { 3, ShaderStage::Compute, reg.getBuffer("SceneCameraData") },
                                                        { 4, ShaderStage::Compute, &kernelSampleBuffer } });
    ComputeState& ssaoComputeState = reg.createComputeState(Shader::createCompute("ssao/ssao.comp"), { &ssaoBindingSet });

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        static float kernelRadius = 0.58f;
        ImGui::SliderFloat("Kernel radius (m)", &kernelRadius, 0.01f, 1.5f);

        static float kernelExponent = 1.75f;
        ImGui::SliderFloat("Kernel exponent", &kernelExponent, 0.5f, 5.0f);

        //static bool applyBlur = true;
        //ImGui::Checkbox("Apply blur", &applyBlur);

        cmdList.setComputeState(ssaoComputeState);
        cmdList.bindSet(ssaoBindingSet, 0);

        cmdList.setNamedUniform("targetSize", appState.windowExtent());
        cmdList.setNamedUniform("kernelRadius", kernelRadius);
        cmdList.setNamedUniform("kernelExponent", kernelExponent);
        cmdList.setNamedUniform("kernelSampleCount", KernelSampleCount);

        cmdList.dispatch({ ambientOcclusionTex.extent(), 1 }, { 32, 32, 1 });
        cmdList.textureWriteBarrier(ambientOcclusionTex);

        // TODO: If we don't blur we don't wanna have to make a copy..
        //if (applyBlur) {
        //    cmdList.setComputeState(blurComputeState);
        //    cmdList.dispatch({ resolution, 1 }, { 16, 16, 1 });
        //    cmdList.copyTexture(blurredAmbientOcclusionTex, ambientOcclusionTex);
        //    cmdList.textureWriteBarrier(blurredAmbientOcclusionTex);
        //}

    };
}

std::vector<vec4> SSAONode::generateKernel(int numSamples) const
{
    std::vector<vec4> kernelSamples {};
    kernelSamples.reserve(numSamples);

    moos::Random rng {};
    for (int i = 0; i < numSamples; ++i) {

        // Places samples somewhat randomly in the xy-hemisphere but ensures they appropriately cover the entire radius,
        // with greater density towards the center. I'm sure this can be tweaked a lot.
        vec3 hemisphereSample = vec3(rng.randomFloatInRange(-1.0f, +1.0f),
                                     rng.randomFloatInRange(-1.0f, +1.0f),
                                     rng.randomFloatInRange(0.0f, +1.0f));
        float sampleScale = float(i) / float(numSamples - 1);
        sampleScale = moos::lerp(0.1f, 1.0f, sampleScale * sampleScale);
        vec3 sample = sampleScale * normalize(hemisphereSample);

        kernelSamples.push_back(vec4(sample, 0.0f));
    }

    return kernelSamples;
}
