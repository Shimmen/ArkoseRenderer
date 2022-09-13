#include "MeshViewerApp.h"

#include "asset/AssetImporter.h"
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
#include "utility/FileDialog.h"
#include "utility/FileIO.h"
#include "utility/Input.h"
#include "utility/Profiling.h"
#include <imgui.h>

void MeshViewerApp::setup(Scene& scene, RenderPipeline& pipeline)
{
    SCOPED_PROFILE_ZONE();

    ////////////////////////////////////////////////////////////////////////////
    // Scene setup
    
    m_scene = &scene;

    scene.setupFromDescription({ .maintainRayTracingScene = false });

    auto boxMeshes = scene.loadMeshes("assets/sample/models/Box.glb");
    boxMeshes.front()->transform.setOrientation(ark::axisAngle(ark::globalUp, ark::toRadians(30.0f)));

    /*
    // Spawn a grid of static mesh instances for a little stress test of instances
    Transform t2 = boxMeshes.front()->transform;
    for (int z = 0; z < 50; z++) {
        for (int x = 0; x < 50; x++) {
            t2.setTranslation(vec3(1.5f + 1.5f * x, 0.0f, -1.5f - 1.5f * z));
            scene.createStaticMeshInstance(boxMeshes.front()->mesh, t2);
        }
    }
    */

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
    drawMenuBar();

    ImGuiID dockspace = ImGui::DockSpaceOverViewport(nullptr, ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoDockingInCentralNode);

    drawMeshHierarchyPanel();
    drawMeshMaterialPanel();
    drawMeshPhysicsPanel();

    //ImGui::DockBuilderSplitNode(dockspace, ImGuiDir_Left, 0.2f, nullptr, &dockspace_id);
    //ImGui::SetNextWindowDockID(dockspace, ImGuiCond_Always);

    m_fpsCameraController.update(Input::instance(), deltaTime);
    return true;
}

void MeshViewerApp::drawMenuBar()
{
    bool showNewSceneModalHack = false;
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New empty...", "Ctrl+N")) {
                showNewSceneModalHack = true;
            }
            if (ImGui::MenuItem("Open...", "Ctrl+O")) {
                loadMeshWithDialog();
            }
            if (ImGui::MenuItem("Save...", "Ctrl+S")) {
                saveMeshWithDialog();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Import...", "Ctrl+I")) {
                openImportMeshDialog();
            }
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
            m_scene->unloadAllMeshes();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void MeshViewerApp::drawMeshHierarchyPanel()
{
    ImGui::Begin("Hierarchy");
    if (m_target != nullptr) {

        ImGui::Text(!target().name.empty()
                        ? target().name.data()
                        : "Mesh");

        if (ImGui::BeginTabBar("MeshViewerLODTabBar")) {

            for (uint32_t lodIdx = 0; lodIdx < target().lods.size(); ++lodIdx) {
                std::string lodLabel = std::format("LOD{}", lodIdx);
                if (ImGui::BeginTabItem(lodLabel.c_str())) {

                    m_selectedLod = lodIdx;
                    StaticMeshLOD_NEW& lod = *target().lods[lodIdx];

                    if (m_selectedSegment >= lod.mesh_segments.size()) {
                        m_selectedSegment = 0;
                    }

                    // Preload the cache first time around (or if the segment count is massive)..
                    // We can never have this list grow during rendering of this ImGui frame.
                    if (lod.mesh_segments.size() > m_segmentNameCache.size()) {
                        size_t numSegmentNames = std::max(1'000ull, lod.mesh_segments.size());
                        for (int idx = 0; idx < numSegmentNames; ++idx) {
                            m_segmentNameCache.push_back(std::format("segment{:03}", idx));
                        }
                    }

                    auto itemGetter = [](void* data, int idx, const char** outText) -> bool {
                        auto& segmentNameCache = *reinterpret_cast<std::vector<std::string>*>(data);
                        ARKOSE_ASSERT(idx < segmentNameCache.size());
                        *outText = segmentNameCache[idx].data();
                        return true;
                    };

                    int numSegments = static_cast<int>(lod.mesh_segments.size());
                    int numToDisplay = std::min(numSegments, 15);
                    bool didClickSegment = ImGui::ListBox("Mesh segments", &m_selectedSegment, itemGetter, &m_segmentNameCache, numSegments, numToDisplay);

                    if (didClickSegment) {
                        ARKOSE_LOG(Info, "Clicked on segment '{}'", m_segmentNameCache[m_selectedSegment]);
                    }

                    ImGui::EndTabItem();
                }
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

void MeshViewerApp::drawMeshMaterialPanel()
{
    ImGui::Begin("Materials");
    if (m_target != nullptr) {
        ImGui::Text("TODO!");
    }
    ImGui::End();
}


void MeshViewerApp::drawMeshPhysicsPanel()
{
    ImGui::Begin("Physics");
    if (m_target != nullptr) {
        ImGui::Text("TODO!");
    }
    ImGui::End();
}

void MeshViewerApp::openImportMeshDialog()
{
    std::vector<FileDialog::FilterItem> filterItems = { { "glTF", "gltf,glb" } };

    if (auto maybePath = FileDialog::open(filterItems)) {

        std::string importFilePath = maybePath.value();
        ARKOSE_LOG(Info, "Importing mesh from file '{}'", importFilePath);

        std::string_view importFileName = FileIO::extractFileNameFromPath(importFilePath);
        std::string targetDirectory = std::format("assets/imported/{}", FileIO::removeExtensionFromPath(importFileName));

        AssetImporter importer {};
        ImportResult assets = importer.importAsset(importFilePath, targetDirectory);

        ARKOSE_LOG(Info, "Imported {} static meshes, {} materials, and {} images.",
                   assets.staticMeshes.size(), assets.materials.size(), assets.images.size());
    }
}

void MeshViewerApp::loadMeshWithDialog()
{
    if (auto maybePath = FileDialog::open({ { "Arkose mesh", Arkose::Asset::StaticMeshAssetExtension() } })) {

        std::string openPath = maybePath.value();
        ARKOSE_LOG(Info, "Loading mesh from file '{}'", openPath);

        // TODO: Load the static mesh asset into the scene & gpu-scene!
        //       Keep a reference to the *asset*, but not the runtime mesh

        StaticMeshAsset* staticMesh = StaticMeshAsset::loadFromArkmsh(openPath);
        if (staticMesh != nullptr) {
            m_target = staticMesh;
        }

        //m_scene->unloadAllMeshes();
        //m_targets = m_scene->loadMeshes(openPath);
    }
}

void MeshViewerApp::saveMeshWithDialog()
{
    if (auto maybePath = FileDialog::save({ { "Arkose mesh", Arkose::Asset::StaticMeshAssetExtension() } })) {
        
        std::string savePath = maybePath.value();
        ARKOSE_LOG(Info, "Saving mesh to file '{}'", savePath);
        // TODO: Save all(?) m_targets to savePath

    }
}
