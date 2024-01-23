#include "VisibilityBufferDebugNode.h"

#include "rendering/GpuScene.h"
#include "rendering/RenderPipeline.h"
#include <ark/random.h>
#include <imgui.h>

void VisibilityBufferDebugNode::drawGui()
{
    ImGui::Text("Visualisation mode:");
    if (ImGui::RadioButton("Drawables", m_mode == Mode::Drawables))
        m_mode = Mode::Drawables;
    if (ImGui::RadioButton("Meshlets", m_mode == Mode::Meshlets))
        m_mode = Mode::Meshlets;
    if (ImGui::RadioButton("Primitives", m_mode == Mode::Primitives))
        m_mode = Mode::Primitives;
}

RenderPipelineNode::ExecuteCallback VisibilityBufferDebugNode::construct(GpuScene& scene, Registry& reg)
{
    ARKOSE_ASSERT(reg.hasPreviousNode("Meshlet visibility buffer"));

    Texture& visualizationTexture = reg.createTexture2D(pipeline().renderResolution(), Texture::Format::RGBA8);
    reg.publish("VisibilityBufferDebugVis", visualizationTexture);

    Texture& instanceVisibilityTexture = *reg.getTexture("InstanceVisibilityTexture");
    Texture& triangleVisibilityTexture = *reg.getTexture("TriangleVisibilityTexture");

    BindingSet& bindingSet = reg.createBindingSet({ ShaderBinding::storageTexture(visualizationTexture),
                                                    ShaderBinding::sampledTexture(instanceVisibilityTexture),
                                                    ShaderBinding::sampledTexture(triangleVisibilityTexture) });

    StateBindings stateBindings;
    stateBindings.at(0, bindingSet);

    Shader shader = Shader::createCompute("visibility-buffer/visualizeVisibilityBuffer.comp");
    ComputeState& computeState = reg.createComputeState(shader, stateBindings);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        cmdList.setComputeState(computeState);
        cmdList.setNamedUniform("mode", static_cast<i32>(m_mode));
        cmdList.dispatch(visualizationTexture.extent3D(), { 8, 8, 1 });

    };
}
