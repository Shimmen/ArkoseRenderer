#include "SSAONode.h"

#include "SceneNode.h"
#include "utility/Logging.h"
#include "utility/Profiling.h"
#include <imgui.h>
#include <moos/random.h>

// Shader data
#include "SSAOData.h"

std::string SSAONode::name()
{
    return "ssao";
}

SSAONode::SSAONode(Scene& scene)
    : RenderGraphNode(SSAONode::name())
    , m_scene(scene)
{
}

void SSAONode::constructNode(Registry& reg)
{
    m_kernelSampleCount = 32;
    ASSERT(m_kernelSampleCount <= SSAO_KERNEL_SAMPLE_MAX_COUNT);
    m_kernelSampleBuffer = &reg.createBuffer(generateKernel(m_kernelSampleCount), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::GpuOptimal);
}

RenderGraphNode::ExecuteCallback SSAONode::constructFrame(Registry& reg) const
{
    SCOPED_PROFILE_ZONE();

    // TODO: Handle resource modifications! For proper async handling
    Texture* sceneOpaqueDepth = reg.getTexture("g-buffer", "depth").value_or(nullptr);
    Texture* sceneOpaqueNormals = reg.getTexture("g-buffer", "normal").value_or(nullptr);

    Texture& ambientOcclusionTex = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA16F); //Texture::Format::R16F);
    reg.publish("ambient-occlusion", ambientOcclusionTex);

    BindingSet& ssaoBindingSet = reg.createBindingSet({ { 0, ShaderStageCompute, &ambientOcclusionTex, ShaderBindingType::StorageImage },
                                                        { 1, ShaderStageCompute, sceneOpaqueDepth, ShaderBindingType::TextureSampler },
                                                        { 2, ShaderStageCompute, sceneOpaqueNormals, ShaderBindingType::TextureSampler },
                                                        { 3, ShaderStageCompute, reg.getBuffer("scene", "camera") },
                                                        { 4, ShaderStageCompute, m_kernelSampleBuffer } });
    ComputeState& ssaoComputeState = reg.createComputeState(Shader::createCompute("ssao/ssao.comp"), { &ssaoBindingSet });

    return [&](const AppState& appState, CommandList& cmdList) {

        static float kernelRadius = 0.58f;
        ImGui::SliderFloat("Kernel radius (m)", &kernelRadius, 0.01f, 1.5f);

        static float kernelExponent = 4.4f;
        ImGui::SliderFloat("Kernel exponent", &kernelExponent, 0.5f, 5.0f);

        //static bool applyBlur = true;
        //ImGui::Checkbox("Apply blur", &applyBlur);

        cmdList.setComputeState(ssaoComputeState);
        cmdList.bindSet(ssaoBindingSet, 0);

        cmdList.setNamedUniform("targetSize", appState.windowExtent());
        cmdList.setNamedUniform("kernelRadius", kernelRadius);
        cmdList.setNamedUniform("kernelExponent", kernelExponent);
        cmdList.setNamedUniform("kernelSampleCount", m_kernelSampleCount);

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
