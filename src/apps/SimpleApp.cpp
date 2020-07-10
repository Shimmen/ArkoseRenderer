#include "SimpleApp.h"

#include "rendering/nodes/FinalPostFxNode.h"
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
    graph.addNode<GBufferNode>(scene());
    graph.addNode<ShadowMapNode>(scene());
    graph.addNode<SlowForwardRenderNode>(scene());
    graph.addNode<FinalPostFxNode>(scene());
}

void SimpleApp::update(float elapsedTime, float deltaTime)
{
    const Input& input = Input::instance();
    scene().camera().update(input, GlobalState::get().windowExtent(), deltaTime);
}
