#include "ShowcaseApp.h"

#include "rendering/nodes/BloomNode.h"
#include "rendering/nodes/DiffuseGINode.h"
#include "rendering/nodes/ExposureNode.h"
#include "rendering/nodes/ForwardRenderNode.h"
#include "rendering/nodes/GBufferNode.h"
#include "rendering/nodes/PickingNode.h"
#include "rendering/nodes/SceneNode.h"
#include "rendering/nodes/ShadowMapNode.h"
#include "rendering/nodes/SkyViewNode.h"
#include "rendering/scene/models/GltfModel.h"
#include "utility/GlobalState.h"
#include "utility/Input.h"
#include "utility/Logging.h"
#include <imgui.h>

std::vector<Backend::Capability> ShowcaseApp::requiredCapabilities()
{
    return { Backend::Capability::ShaderTextureArrayDynamicIndexing,
             Backend::Capability::ShaderBufferArrayDynamicIndexing };
}

std::vector<Backend::Capability> ShowcaseApp::optionalCapabilities()
{
    return {};
}

void ShowcaseApp::setup(RenderGraph& graph)
{
    LogInfo("Loading scene\n");
    scene().loadFromFile("assets/sample/cornell-box.json");
    LogInfo("Done loading scene\n");

    LogInfo("Setting up render graph\n");

    graph.addNode<SceneNode>(scene());
    graph.addNode<PickingNode>(scene());

    graph.addNode<ShadowMapNode>(scene());

    graph.addNode<GBufferNode>(scene());
    graph.addNode<ForwardRenderNode>(scene());
    graph.addNode<SkyViewNode>(scene());

    auto probeGridDescription = DiffuseGINode::ProbeGridDescription {
        .gridDimensions = { 8, 8, 8 },
        .probeSpacing = { 1, 1, 1 },
        .offsetToFirst = vec3(-4, -2, -4)
    };
    graph.addNode<DiffuseGINode>(scene(), probeGridDescription);

    graph.addNode<BloomNode>(scene());

    graph.addNode<ExposureNode>(scene());

    graph.addNode("final", [](Registry& reg) {
        // TODO: We should probably use compute for this now.. we don't require interpolation or any type of depth writing etc.
        std::vector<vec2> fullScreenTriangle { { -1, -3 }, { -1, 1 }, { 3, 1 } };
        Buffer& vertexBuffer = reg.createBuffer(std::move(fullScreenTriangle), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal);
        VertexLayout vertexLayout = VertexLayout { sizeof(vec2), { { 0, VertexAttributeType::Float2, 0 } } };

        BindingSet& bindingSet = reg.createBindingSet({ { 0, ShaderStageFragment, reg.getTexture("forward", "color").value(), ShaderBindingType::TextureSampler } });
        Shader shader = Shader::createBasicRasterize("final/simple.vert", "final/simple.frag");
        RenderStateBuilder renderStateBuilder { reg.windowRenderTarget(), shader, vertexLayout };
        renderStateBuilder.addBindingSet(bindingSet);
        renderStateBuilder.writeDepth = false;
        renderStateBuilder.testDepth = false;

        RenderState& renderState = reg.createRenderState(renderStateBuilder);

        return [&](const AppState& appState, CommandList& cmdList) {
            cmdList.beginRendering(renderState, ClearColor(0.5f, 0.1f, 0.5f), 1.0f);
            cmdList.bindSet(bindingSet, 0);
            cmdList.draw(vertexBuffer, 3);
        };
    });

    LogInfo("Done setting up render graph\n");
}

void ShowcaseApp::update(float elapsedTime, float deltaTime)
{
    const Input& input = Input::instance();
    scene().camera().update(input, GlobalState::get().windowExtent(), deltaTime);
}
