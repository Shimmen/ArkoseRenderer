#include "SimpleApp.h"

#include "rendering/nodes/FinalPostFxNode.h"
#include "rendering/nodes/GBufferNode.h"
#include "rendering/nodes/SceneUniformNode.h"
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
    m_scene = Scene::loadFromFile("assets/sample/cornell-box.json");

    graph.addNode<SceneUniformNode>(*m_scene);
    graph.addNode<GBufferNode>(*m_scene);
    graph.addNode<ShadowMapNode>(*m_scene);
    graph.addNode<SlowForwardRenderNode>(*m_scene);
    graph.addNode<FinalPostFxNode>(*m_scene);
}

void SimpleApp::update(float elapsedTime, float deltaTime)
{
    ImGui::Begin("SimpleApp");
    ImGui::ColorEdit3("Sun color", value_ptr(m_scene->sun().color));
    ImGui::SliderFloat("Sun intensity", &m_scene->sun().intensity, 0.0f, 50.0f);
    ImGui::SliderFloat("Environment", &m_scene->environmentMultiplier(), 0.0f, 5.0f);
    ImGui::End();

    const Input& input = Input::instance();
    m_scene->camera().update(input, GlobalState::get().windowExtent(), deltaTime);
}
