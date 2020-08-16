#include "SkyViewNode.h"

#include <imgui.h>

SkyViewNode::SkyViewNode(Scene& scene)
    : RenderGraphNode(SkyViewNode::name())
    , m_scene(scene)
{
}

RenderGraphNode::ExecuteCallback SkyViewNode::constructFrame(Registry& reg) const
{
    Texture& targetImage = *reg.getTexture("forward", "color").value();
    Texture& depthImage = *reg.getTexture("g-buffer", "depth").value();

    Texture& skyViewTexture = m_scene.environmentMap().empty()
        ? reg.createPixelTexture(vec4(1.0f), true)
        : reg.loadTexture2D(m_scene.environmentMap(), true, false);
    reg.publish("skyView", skyViewTexture);

    BindingSet& skyViewBindingSet = reg.createBindingSet({ { 0, ShaderStageCompute, reg.getBuffer("scene", "camera") },
                                                           { 1, ShaderStageCompute, &targetImage, ShaderBindingType::StorageImage },
                                                           { 2, ShaderStageCompute, &depthImage, ShaderBindingType::TextureSampler },
                                                           { 3, ShaderStageCompute, &skyViewTexture, ShaderBindingType::TextureSampler } });

    ComputeState& skyViewComputeState = reg.createComputeState(Shader::createCompute("post/sky-view.comp"), { &skyViewBindingSet });

    return [&](const AppState& appState, CommandList& cmdList) {
        cmdList.setComputeState(skyViewComputeState);
        cmdList.bindSet(skyViewBindingSet, 0);

        ImGui::SliderFloat("Multiplier", &m_scene.environmentMultiplier(), 0.0f, 5.0f);
        float envMultiplier = m_scene.environmentMultiplier();
        cmdList.pushConstant(ShaderStageCompute, envMultiplier);

        cmdList.dispatch(targetImage.extent(), { 16, 16, 1 });
    };
}
