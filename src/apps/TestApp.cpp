#include "TestApp.h"

#include "rendering/nodes/FinalPostFxNode.h"
#include "rendering/nodes/ForwardRenderNode.h"
#include "rendering/nodes/GBufferNode.h"
#include "rendering/nodes/RTAccelerationStructures.h"
#include "rendering/nodes/RTAmbientOcclusion.h"
#include "rendering/nodes/RTDiffuseGINode.h"
#include "rendering/nodes/RTFirstHitNode.h"
#include "rendering/nodes/RTReflectionsNode.h"
#include "rendering/nodes/SceneUniformNode.h"
#include "rendering/nodes/ShadowMapNode.h"
#include "rendering/nodes/SlowForwardRenderNode.h"
#include "rendering/scene/models/GltfModel.h"
#include "utility/GlobalState.h"
#include "utility/Input.h"
#include <imgui.h>
#include <mooslib/transform.h>

std::vector<Backend::Capability> TestApp::requiredCapabilities()
{
    return { Backend::Capability::RtxRayTracing,
             Backend::Capability::ShaderTextureArrayDynamicIndexing,
             Backend::Capability::ShaderStorageBufferDynamicIndexing };
}

std::vector<Backend::Capability> TestApp::optionalCapabilities()
{
    return {};
}

void TestApp::setup(RenderGraph& graph)
{
    //m_scene = Scene::loadFromFile("assets/sample/sponza.json");
    m_scene = Scene::loadFromFile("assets/sample/cornell-box.json");

    bool rtxOn = true;
    bool firstHit = true;

    graph.addNode<SceneUniformNode>(*m_scene);
    graph.addNode<GBufferNode>(*m_scene);
    graph.addNode<ShadowMapNode>(*m_scene);
    graph.addNode<SlowForwardRenderNode>(*m_scene);
    if (rtxOn) {
        graph.addNode<RTAccelerationStructures>(*m_scene);
        graph.addNode<RTAmbientOcclusion>(*m_scene);
        graph.addNode<RTDiffuseGINode>(*m_scene);
        if (firstHit) {
            graph.addNode<RTFirstHitNode>(*m_scene);
        }
    }
    graph.addNode<FinalPostFxNode>(*m_scene);
}

void TestApp::update(float elapsedTime, float deltaTime)
{
    ImGui::Begin("TestApp");
    ImGui::ColorEdit3("Sun color", value_ptr(m_scene->sun().color));
    ImGui::SliderFloat("Sun intensity", &m_scene->sun().intensity, 0.0f, 50.0f);
    ImGui::SliderFloat("Environment", &m_scene->environmentMultiplier(), 0.0f, 5.0f);
    if (ImGui::CollapsingHeader("Cameras")) {
        m_scene->cameraGui();
    }
    ImGui::End();

    ImGui::Begin("Metrics");
    float ms = deltaTime * 1000.0f;
    ImGui::Text("Frame time: %.3f ms/frame", ms);
    ImGui::End();

    const Input& input = Input::instance();
    m_scene->camera().update(input, GlobalState::get().windowExtent(), deltaTime);
}
