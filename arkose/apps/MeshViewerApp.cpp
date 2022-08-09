#include "MeshViewerApp.h"

#include "rendering/nodes/BloomNode.h"
#include "rendering/nodes/CullingNode.h"
#include "rendering/nodes/FinalNode.h"
#include "rendering/nodes/ForwardRenderNode.h"
#include "rendering/nodes/SSAONode.h"
#include "rendering/nodes/SkyViewNode.h"
#include "rendering/nodes/TAANode.h"
#include "rendering/nodes/TonemapNode.h"
#include "scene/Scene.h"
#include "scene/camera/Camera.h"
#include "scene/lights/DirectionalLight.h"
#include "utility/Input.h"
#include "utility/Profiling.h"

#include <imgui.h>
#include <nfd.h>

void MeshViewerApp::setup(Scene& scene, RenderPipeline& pipeline)
{
    SCOPED_PROFILE_ZONE();

    ////////////////////////////////////////////////////////////////////////////
    // Scene setup
    
    scene.setupFromDescription({ .maintainRayTracingScene = false });

    auto boxMeshes = scene.loadMeshes("assets/sample/models/Box.glb");
    boxMeshes.front()->transform.setOrientation(ark::axisAngle(ark::globalUp, ark::toRadians(30.0f)));

    scene.setAmbientIlluminance(600.0f);
    scene.setEnvironmentMap({ .assetPath = "assets/sample/hdri/tiergarten_2k.hdr",
                              .brightnessFactor = 5000.0f });

    vec3 sunDirecton = normalize(vec3(-1.0f, -1.0f, -1.0f));
    DirectionalLight& sun = scene.addLight(std::make_unique<DirectionalLight>(vec3(1.0f), 90'000.0f, sunDirecton));

    Camera& camera = scene.addCamera("default", true);
    camera.lookAt(vec3(0, 1.0f, 4.0f), vec3(0, 0, 0));
    camera.setManualExposureParameters(11.0f, 1.0f / 125.0f, 400.0f);
    m_fpsCameraController.takeControlOfCamera(camera);

    ////////////////////////////////////////////////////////////////////////////
    // Render pipeline setup

    pipeline.addNode<CullingNode>();
    pipeline.addNode<ForwardRenderNode>();
    // TODO: Maybe add some IBL for this?
    pipeline.addNode<SkyViewNode>();

    pipeline.addNode<TonemapNode>("SceneColor");
    pipeline.addNode<TAANode>(scene.camera());
    
    FinalNode& finalNode = pipeline.addNode<FinalNode>("SceneColorLDR");
    finalNode.setRenderFilmGrain(false);
    finalNode.setRenderVignette(false);
}

bool MeshViewerApp::update(Scene& scene, float elapsedTime, float deltaTime)
{
    ImGui::DockSpaceOverViewport(nullptr, ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoDockingInCentralNode);

    bool showNewSceneModalHack = false;
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New empty...", "Ctrl+N")) { showNewSceneModalHack = true; }
            if (ImGui::MenuItem("Open...", "Ctrl+O")) { loadMeshWithDialog(scene); }
            if (ImGui::MenuItem("Save...", "Ctrl+S")) { saveMeshWithDialog(scene); }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // See https://github.com/ocornut/imgui/issues/331 for more info on this bug and hack.
    if (showNewSceneModalHack) {
        ImGui::OpenPopup("Create a new scene");
        showNewSceneModalHack = false;
    }
    if (ImGui::BeginPopupModal("Create a new scene")) {
        ImGui::Text("You are about to create a scene and potentially loose any unchanged settings. Are you sure you want to proceed?");
        if (ImGui::Button("Yes")) {
            scene.unloadAllMeshes();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    m_fpsCameraController.update(Input::instance(), deltaTime);
    return true;
}

void MeshViewerApp::loadMeshWithDialog(Scene& scene)
{
    if (NFD_Init() != NFD_OKAY) {
        ARKOSE_LOG(Fatal, "Can't init NFD!");
    }

    nfdchar_t* outPath;
    nfdfilteritem_t filterItem[] = { { "Source meshes", "gltf,glb" },
                                     { "Binary arkose data", "arkblob" } };
    if (NFD_OpenDialog(&outPath, filterItem, 2, nullptr) == NFD_OKAY) {
        
        ARKOSE_LOG(Info, "Loading mesh from file '{}'", outPath);
        scene.unloadAllMeshes();
        m_targets = scene.loadMeshes(outPath);
        NFD_FreePath(outPath);

    } else if (const char* error = NFD_GetError()) {
        ARKOSE_LOG(Error, "Open file dialog error: {}.", error);
        NFD_ClearError();
    }

    NFD_Quit();
}

void MeshViewerApp::saveMeshWithDialog(Scene& scene)
{
    if (NFD_Init() != NFD_OKAY) {
        ARKOSE_LOG(Fatal, "Can't init NFD!");
    }

    nfdchar_t* savePath;
    nfdfilteritem_t filterItem[] = { { "Binary arkose data", "arkblob" } };
    if (NFD_SaveDialog(&savePath, filterItem, 1, "assets/", nullptr) == NFD_OKAY) {
        
        ARKOSE_LOG(Info, "Saving mesh to file '{}'", savePath);
        // TODO: Save all(?) m_targets to savePath
        NFD_FreePath(savePath);

    } else if (const char* error = NFD_GetError()) {
        ARKOSE_LOG(Error, "Save file dialog error: {}.", error);
        NFD_ClearError();
    }

    NFD_Quit();
}

