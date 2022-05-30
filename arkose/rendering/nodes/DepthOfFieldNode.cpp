#include "DepthOfFieldNode.h"

#include "rendering/scene/GpuScene.h"
#include <imgui.h>

RenderPipelineNode::ExecuteCallback DepthOfFieldNode::construct(GpuScene& scene, Registry& reg)
{
    Buffer& sceneCameraBuffer = *reg.getBuffer("SceneCameraData");
    Texture& sceneColor = *reg.getTexture("SceneColor");
    Texture& sceneDepth= *reg.getTexture("SceneDepth");

    Texture& circleOfConfusionTex = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::R16F, Texture::Filters::nearest());
    BindingSet& calculateCocBindingSet = reg.createBindingSet({ ShaderBinding::storageTexture(circleOfConfusionTex, ShaderStage::Compute),
                                                                ShaderBinding::sampledTexture(sceneDepth, ShaderStage::Compute),
                                                                ShaderBinding::constantBuffer(sceneCameraBuffer, ShaderStage::Compute) });
    Shader calculateCocShader = Shader::createCompute("depth-of-field/calculateCoc.comp");
    ComputeState& calculateCocState = reg.createComputeState(calculateCocShader, { &calculateCocBindingSet });

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        Extent2D targetSize = reg.windowRenderTarget().extent();
        Camera& camera = scene.scene().camera();

        // Calculte CoC at full resolution
        cmdList.setComputeState(calculateCocState);
        cmdList.bindSet(calculateCocBindingSet, 0);
        cmdList.setNamedUniform("targetSize", targetSize);
        cmdList.setNamedUniform("focusDepth", camera.focusDepth());
        cmdList.dispatch(targetSize, { 8, 8, 1 });

        cmdList.textureWriteBarrier(circleOfConfusionTex);

        static bool outputCoc = false;
        ImGui::Checkbox("Output CoC", &outputCoc);
        if (outputCoc) {
            cmdList.copyTexture(circleOfConfusionTex, sceneColor);
        }

        // TODO: Perform blur!

    };
}