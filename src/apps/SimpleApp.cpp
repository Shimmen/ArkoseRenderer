#include "SimpleApp.h"

#include "rendering/nodes/ExposureNode.h"
#include "rendering/nodes/GBufferNode.h"
#include "rendering/nodes/PickingNode.h"
#include "rendering/nodes/SceneNode.h"
#include "rendering/nodes/ShadowMapNode.h"
#include "rendering/nodes/SlowForwardRenderNode.h"
#include "rendering/scene/models/GltfModel.h"
#include "utility/GlobalState.h"
#include "utility/Input.h"
#include <imgui.h>

std::vector<Backend::Capability> SimpleApp::requiredCapabilities()
{
    return {};
}

std::vector<Backend::Capability> SimpleApp::optionalCapabilities()
{
    return { Backend::Capability::ShaderTextureArrayDynamicIndexing,
             Backend::Capability::ShaderBufferArrayDynamicIndexing };
}

void SimpleApp::setup(RenderGraph& graph)
{
    scene().loadFromFile("assets/sample/cornell-box.json");

    graph.addNode<SceneNode>(scene());
    graph.addNode<PickingNode>(scene());

    graph.addNode<ShadowMapNode>(scene());

    graph.addNode<GBufferNode>(scene());
    graph.addNode<SlowForwardRenderNode>(scene());

    graph.addNode<ExposureNode>(scene());

    graph.addNode("final", [](Registry& reg) {
        std::vector<vec2> fullScreenTriangle { { -1, -3 }, { -1, 1 }, { 3, 1 } };
        Buffer& vertexBuffer = reg.createBuffer(std::move(fullScreenTriangle), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal);
        VertexLayout vertexLayout = VertexLayout { sizeof(vec2), { { 0, VertexAttributeType::Float2, 0 } } };

        BindingSet& sourceBindingSet = reg.createBindingSet({ { 0, ShaderStageFragment, reg.getTexture("forward", "color").value(), ShaderBindingType::TextureSampler } });
        BindingSet& envBindingSet = reg.createBindingSet({ { 0, ShaderStageVertex, reg.getBuffer("scene", "camera") },
                                                           { 1, ShaderStageFragment, reg.getTexture("scene", "environmentMap").value_or(&reg.createPixelTexture(vec4(1), true)), ShaderBindingType::TextureSampler },
                                                           { 2, ShaderStageFragment, reg.getTexture("g-buffer", "depth").value(), ShaderBindingType::TextureSampler },
                                                           { 3, ShaderStageFragment, reg.getBuffer("scene", "environmentData") } });

        Shader shader = Shader::createBasicRasterize("final/simple.vert", "final/simple.frag");
        RenderStateBuilder renderStateBuilder { reg.windowRenderTarget(), shader, vertexLayout };
        renderStateBuilder.addBindingSet(sourceBindingSet).addBindingSet(envBindingSet);
        renderStateBuilder.writeDepth = false;
        renderStateBuilder.testDepth = false;

        RenderState& renderState = reg.createRenderState(renderStateBuilder);

        return [&](const AppState& appState, CommandList& cmdList) {
            cmdList.beginRendering(renderState, ClearColor(0.5f, 0.1f, 0.5f), 1.0f);
            cmdList.bindSet(sourceBindingSet, 0);
            cmdList.bindSet(envBindingSet, 1);
            cmdList.draw(vertexBuffer, 3);
        };
    });
}

void SimpleApp::update(float elapsedTime, float deltaTime)
{
    const Input& input = Input::instance();
    scene().camera().update(input, GlobalState::get().windowExtent(), deltaTime);
}
