#include "ShowcaseApp.h"

#include "rendering/nodes/BloomNode.h"
#include "rendering/nodes/CullingNode.h"
#include "rendering/nodes/DDGINode.h"
#include "rendering/nodes/DDGIProbeDebug.h"
#include "rendering/nodes/DepthOfFieldNode.h"
#include "rendering/nodes/DirectionalLightShadowNode.h"
#include "rendering/nodes/ForwardRenderNode.h"
#include "rendering/nodes/FinalNode.h"
#include "rendering/nodes/FXAANode.h"
#include "rendering/nodes/GIComposeNode.h"
#include "rendering/nodes/LocalLightShadowNode.h"
#include "rendering/nodes/PrepassNode.h"
#include "rendering/nodes/PickingNode.h"
#include "rendering/nodes/RTVisualisationNode.h"
#include "rendering/nodes/RTReflectionsNode.h"
#include "rendering/nodes/SkyViewNode.h"
#include "rendering/nodes/SSAONode.h"
#include "rendering/nodes/TAANode.h"
#include "rendering/nodes/TonemapNode.h"
#include "scene/Scene.h"
#include "scene/camera/Camera.h"
#include "scene/lights/DirectionalLight.h"
#include "utility/Input.h"
#include "utility/Profiling.h"
#include <imgui.h>

constexpr bool keepRenderDocCompatible = false;
constexpr bool rtxOn = true && !keepRenderDocCompatible;

std::vector<Backend::Capability> ShowcaseApp::requiredCapabilities()
{
    if (rtxOn) {
        return { Backend::Capability::RayTracing };
    } else {
        return {};
    }
}

void ShowcaseApp::setup(Scene& scene, RenderPipeline& pipeline)
{
    SCOPED_PROFILE_ZONE();

    scene.setupFromDescription({ .path = "assets/sample/Sponza.arklvl",
                                 .maintainRayTracingScene = rtxOn });

    Camera& camera = scene.camera();
    m_fpsCameraController.takeControlOfCamera(camera);

    if (!scene.hasProbeGrid()) {
        scene.generateProbeGridFromBoundingBox();
    }

    pipeline.addNode<PickingNode>();

    if (rtxOn) {
        pipeline.addNode<DDGINode>();
    } else {
        scene.setAmbientIlluminance(250.0f);
    }

    pipeline.addNode<CullingNode>();
    pipeline.addNode<PrepassNode>(PrepassMode::AllOpaquePixels);

    pipeline.addNode<DirectionalLightShadowNode>();
    pipeline.addNode<LocalLightShadowNode>();

    pipeline.addNode<ForwardRenderNode>();

    if (rtxOn) {
        //pipeline.addNode<RTReflectionsNode>();
    }

    pipeline.addNode<SSAONode>();
    pipeline.addNode<GIComposeNode>();
    
    pipeline.addNode<SkyViewNode>();

    pipeline.addNode<BloomNode>();

    auto& dofNode = pipeline.addNode<DepthOfFieldNode>();
    dofNode.setEnabled(false);

    if (rtxOn) {
        pipeline.addNode<DDGIProbeDebug>();
    }

    std::string sceneTexture = "SceneColor";
    const std::string finalTextureToScreen = "SceneColorLDR";
    const AntiAliasing antiAliasingMode = AntiAliasing::TAA;

    if (rtxOn) {
        // Uncomment for ray tracing visualisations
        //pipeline.addNode<RTVisualisationNode>(RTVisualisationNode::Mode::DirectLight); sceneTexture = "RTVisualisation";
    }

    pipeline.addNode<TonemapNode>(sceneTexture);

    switch (antiAliasingMode) {
    case AntiAliasing::FXAA:
        pipeline.addNode<FXAANode>();
        break;
    case AntiAliasing::TAA:
        pipeline.addNode<TAANode>(scene.camera());
        break;
    }

    pipeline.addNode<FinalNode>(finalTextureToScreen);

    // Save reference to the render pipeline for GUI purposes
    m_renderPipeline = &pipeline;
}

bool ShowcaseApp::update(Scene& scene, float elapsedTime, float deltaTime)
{
    const Input& input = Input::instance();

    // Toggle GUI with the ` key
    if (input.wasKeyReleased(Key::GraveAccent)) {
        m_guiEnabled = !m_guiEnabled;
    }

    bool exitRequested = false;
    if (m_guiEnabled) {
        exitRequested = drawGui(scene);
    }

    m_fpsCameraController.update(input, deltaTime);

    float sunRotation = 0.0f;
    sunRotation -= input.isKeyDown(Key::Left) ? 1.0f : 0.0f;
    sunRotation += input.isKeyDown(Key::Right) ? 1.0f : 0.0f;
    quat rotation = axisAngle(ark::globalRight, sunRotation * deltaTime * 0.2f);

    DirectionalLight& sun = *scene.firstDirectionalLight();
    sun.direction = ark::rotateVector(rotation, sun.direction);

    return !exitRequested;
}

bool ShowcaseApp::drawGui(Scene& scene)
{
    bool exitRequested = false;

    static bool showAbout = false;
    static bool showCameraGui = false;
    static bool showSceneGui = false;
    static bool showGpuSceneGui = false;
    static bool showVramUsageGui = false;
    static bool showRenderPipelineGui = true;

    if (showAbout) {
        if (ImGui::Begin("About", &showAbout, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
            ImGui::Text("Arkose Renderer");
            ImGui::Separator();
            ImGui::Text("This is a showcase of most things that this renderer can do, please enjoy!");
            ImGui::Separator();
            ImGui::Text("By Simon Moos | @SimonMoos | http://simon-moos.com | https://github.com/Shimmen/");
            ImGui::Text("Arkose Renderer is licensed under the MIT License, see LICENSE for more information.");
        }
        ImGui::End();
    }

    if (showCameraGui) {
        if (ImGui::Begin("Camera", &showCameraGui, ImGuiWindowFlags_NoCollapse)) {
            scene.camera().drawGui();
        }
        ImGui::End();
    }
    
    if (showSceneGui) {
        if (ImGui::Begin("Scene settings", &showSceneGui, ImGuiWindowFlags_NoCollapse)) {
            scene.drawSettingsGui();
        }
        ImGui::End();
    }
    
    if (showGpuSceneGui) { 
        if (ImGui::Begin("GPU scene stats", &showGpuSceneGui, ImGuiWindowFlags_NoCollapse)) {
            scene.gpuScene().drawStatsGui();
        }
        ImGui::End();
    }

    if (showVramUsageGui) {
        if (ImGui::Begin("VRAM usage", &showVramUsageGui, ImGuiWindowFlags_NoCollapse)) {
            scene.gpuScene().drawVramUsageGui();
        }
        ImGui::End();
    }

    if (m_renderPipeline && showRenderPipelineGui) {
        if (ImGui::Begin("Render Pipeline", &showRenderPipelineGui)) {
            m_renderPipeline->drawGui();
        }
        ImGui::End();
    }

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            exitRequested = ImGui::MenuItem("Quit");
            ImGui::Separator();
            ImGui::MenuItem("About...", nullptr, &showAbout);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Scene settings", nullptr, &showSceneGui);
            ImGui::MenuItem("Render pipeline", nullptr, &showRenderPipelineGui);
            ImGui::MenuItem("Camera", nullptr, &showCameraGui);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Stats")) {
            ImGui::MenuItem("GPU scene stats", nullptr, &showGpuSceneGui);
            ImGui::MenuItem("VRAM usage stats", nullptr, &showVramUsageGui);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    return exitRequested;
}
