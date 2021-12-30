#include "AutoExposureNode.h"

#include "CameraState.h"
#include "utility/Profiling.h"
#include <imgui.h>
#include <moos/vector.h>

AutoExposureNode::AutoExposureNode(Scene& scene)
    : RenderPipelineNode(AutoExposureNode::name())
    , m_scene(scene)
{
}

RenderPipelineNode::ExecuteCallback AutoExposureNode::constructFrame(Registry& reg) const
{
    SCOPED_PROFILE_ZONE();

    Texture& logLuminanceTexture = reg.createTexture2D({ 512, 512 }, Texture::Format::R32F, Texture::Filters::linear(), Texture::Mipmap::Nearest);

    Texture& targetImage = *reg.getTexture("SceneColor");
    BindingSet& logLumBindingSet = reg.createBindingSet({ { 0, ShaderStageCompute, &targetImage, ShaderBindingType::TextureSampler },
                                                          { 1, ShaderStageCompute, &logLuminanceTexture, ShaderBindingType::StorageImage } });
    ComputeState& logLumComputeState = reg.createComputeState(Shader::createCompute("post/logLuminance.comp"), { &logLumBindingSet });

    Buffer& passDataBuffer = reg.createBuffer(2 * sizeof(float), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::TransferOptimal);
    passDataBuffer.setName("ExposurePassData");

    BindingSet& sourceDataBindingSet = reg.createBindingSet({ { 0, ShaderStageCompute, reg.getBuffer("SceneCameraData") },
                                                              { 1, ShaderStageCompute, &logLuminanceTexture, ShaderBindingType::TextureSampler } });
    BindingSet& targetDataBindingSet = reg.createBindingSet({ { 0, ShaderStageCompute, &passDataBuffer } });
    ComputeState& exposeComputeState = reg.createComputeState(Shader::createCompute("post/expose.comp"), { &sourceDataBindingSet, &targetDataBindingSet });

    return [&](const AppState& appState, CommandList& cmdList) {
        FpsCamera& camera = m_scene.camera();
        if (!camera.useAutomaticExposure)
            return;

        // Calculate log-luminance over the whole image
        cmdList.setComputeState(logLumComputeState);
        cmdList.bindSet(logLumBindingSet, 0);
        cmdList.setNamedUniform("targetSize", logLuminanceTexture.extent());
        cmdList.dispatch(logLuminanceTexture.extent(), { 16, 16, 1 });

        // Compute average log-luminance by creating mipmaps
        cmdList.generateMipmaps(logLuminanceTexture);

        // FIXME: Don't use hardcoded event index! Maybe we should have some event resource type?
        static bool firstTimeAround  = true;
        cmdList.waitEvent(1, firstTimeAround ? PipelineStage::Host : PipelineStage::Compute);
        firstTimeAround = false;
        cmdList.resetEvent(1, PipelineStage::Compute);
        {
            cmdList.setComputeState(exposeComputeState);
            cmdList.bindSet(sourceDataBindingSet, 0);
            cmdList.bindSet(targetDataBindingSet, 1);
            BindingSet& prevData = m_lastFrameBindingSet.has_value()
                ? *m_lastFrameBindingSet.value()
                : targetDataBindingSet;
            cmdList.bindSet(prevData, 2);

            cmdList.setNamedUniform("deltaTime", (float)appState.deltaTime());
            cmdList.setNamedUniform("adaptionRate", appState.isRelativeFirstFrame() ? 9999.99f : camera.adaptionRate);

            cmdList.dispatch(1, 1, 1);
        }
        cmdList.signalEvent(1, PipelineStage::Compute);

        m_lastFrameBindingSet = &targetDataBindingSet;
        m_scene.setNextFrameExposureResultBuffer(passDataBuffer);
    };
}
