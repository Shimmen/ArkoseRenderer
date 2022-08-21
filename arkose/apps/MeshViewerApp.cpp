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
#include "utility/FileDialog.h"
#include "utility/Input.h"
#include "utility/Profiling.h"
#include <imgui.h>

// For texture compression testing
#include "pack/Arkblob.h"
#include "pack/TextureCompressor.h"

void MeshViewerApp::setup(Scene& scene, RenderPipeline& pipeline)
{
    SCOPED_PROFILE_ZONE();

    // Test compressed textures
    {
        SCOPED_PROFILE_ZONE_NAMED("Arkblob & texture compression test");

        Image* image = Image::load("assets/test-pattern.png", Image::PixelType::RGBA);

        TextureCompressor compressor {};
        std::unique_ptr<Image> compressedImage = compressor.compressBC7(*image);

        if (compressedImage) {

            std::unique_ptr<Arkblob> imageBlobWrite = Arkblob::makeImageBlob(*compressedImage);
            imageBlobWrite->writeToFile("assets/test-pattern"); // extension optional, derived from type

            {
                SCOPED_PROFILE_ZONE_NAMED("Waiting for files to resolve...");
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }

            std::unique_ptr<Image> reconstructedImage = Arkblob::readImageFromBlob("assets/test-pattern.arkimg");
        }
    }

    ////////////////////////////////////////////////////////////////////////////
    // Scene setup
    
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
    std::vector<FileDialog::FilterItem> filterItems = { { "Source meshes", "gltf,glb" },
                                                        { "Arkblob mesh", Arkblob::fileExtensionForType(Arkblob::Type::Mesh) } };

    if (auto maybePath = FileDialog::open(filterItems)) {

        std::string openPath = maybePath.value();
        ARKOSE_LOG(Info, "Loading mesh from file '{}'", openPath);

        scene.unloadAllMeshes();
        m_targets = scene.loadMeshes(openPath);
    }
}

void MeshViewerApp::saveMeshWithDialog(Scene& scene)
{
    if (auto maybePath = FileDialog::save({ { "Binary arkose data", Arkblob::fileExtensionForType(Arkblob::Type::Mesh) } })) {
        std::string savePath = maybePath.value();
        ARKOSE_LOG(Info, "Saving mesh to file '{}'", savePath);
        // TODO: Save all(?) m_targets to savePath
    }
}
