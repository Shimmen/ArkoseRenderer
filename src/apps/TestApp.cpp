#include "TestApp.h"

#include "rendering/nodes/FinalPostFxNode.h"
#include "rendering/nodes/ForwardRenderNode.h"
#include "rendering/nodes/GBufferNode.h"
#include "rendering/nodes/RTAccelerationStructures.h"
#include "rendering/nodes/RTAmbientOcclusion.h"
#include "rendering/nodes/RTDiffuseGINode.h"
#include "rendering/nodes/RTFirstHitNode.h"
#include "rendering/nodes/RTReflectionsNode.h"
#include "rendering/nodes/SceneNode.h"
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
             Backend::Capability::ShaderBufferArrayDynamicIndexing };
}

std::vector<Backend::Capability> TestApp::optionalCapabilities()
{
    return {};
}

void TestApp::setup(RenderGraph& graph)
{
    //scene().loadFromFile("assets/sample/sponza.json");
    scene().loadFromFile("assets/sample/cornell-box.json");

    bool rtxOn = true;
    bool firstHit = true;

    graph.addNode<SceneNode>(scene());
    graph.addNode<GBufferNode>(scene());
    graph.addNode<ShadowMapNode>(scene());
    graph.addNode<SlowForwardRenderNode>(scene());
    if (rtxOn) {
        graph.addNode<RTAccelerationStructures>(scene());
        graph.addNode<RTAmbientOcclusion>(scene());
        graph.addNode<RTReflectionsNode>(scene());
        graph.addNode<RTDiffuseGINode>(scene());
        if (firstHit) {
            graph.addNode<RTFirstHitNode>(scene());
        }
    }
    graph.addNode<FinalPostFxNode>(scene());
}

void TestApp::update(float elapsedTime, float deltaTime)
{
    m_frameTimeAvg.report(deltaTime);

    ImGui::Begin("TestApp");
    float avgFrameTime = m_frameTimeAvg.runningAverage() * 1000.0f;
    ImGui::Text("Frame time: %.2f ms/frame", avgFrameTime);
    if (ImGui::CollapsingHeader("Cameras"))
        scene().cameraGui();
    ImGui::End();

    const Input& input = Input::instance();
    scene().camera().update(input, GlobalState::get().windowExtent(), deltaTime);
}
