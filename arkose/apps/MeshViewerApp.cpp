#include "MeshViewerApp.h"

#include "asset/MaterialAsset.h"
#include "asset/import/AssetImporter.h"
#include "input/Input.h"
#include "physics/PhysicsScene.h"
#include "physics/backend/base/PhysicsBackend.h"
#include "rendering/debug/DebugDrawer.h"
#include "rendering/forward/ForwardRenderNode.h"
#include "rendering/forward/PrepassNode.h"
#include "rendering/nodes/BloomNode.h"
#include "rendering/nodes/DebugDrawNode.h"
#include "rendering/nodes/FinalNode.h"
#include "rendering/nodes/SSAONode.h"
#include "rendering/nodes/SkyViewNode.h"
#include "rendering/nodes/TAANode.h"
#include "rendering/nodes/TonemapNode.h"
#include "scene/Scene.h"
#include "scene/camera/Camera.h"
#include "scene/lights/DirectionalLight.h"
#include "utility/FileDialog.h"
#include "utility/FileIO.h"
#include "utility/Profiling.h"
#include <fmt/format.h>
#include <imgui.h>
#include <imgui_internal.h>

void MeshViewerApp::setup(Scene& scene, RenderPipeline& pipeline)
{
    SCOPED_PROFILE_ZONE();

    ////////////////////////////////////////////////////////////////////////////
    // Scene setup
    
    m_scene = &scene;

    scene.setupFromDescription({ .maintainRayTracingScene = false });

    MeshAsset* boxMesh = MeshAsset::load("assets/sample/models/Box/Box.arkmsh");
    StaticMeshInstance& boxInstance = scene.addMesh(boxMesh);
    boxInstance.transform().setOrientation(ark::axisAngle(ark::globalUp, ark::toRadians(30.0f)));

    /*
    // Spawn a grid of static mesh instances for a little stress test of instances
    Transform t2 = boxInstance.transform();
    for (int z = 0; z < 200; z++) {
        for (int x = 0; x < 200; x++) {
            t2.setTranslation(vec3(1.5f + 1.5f * x, 0.0f, -1.5f - 1.5f * z));
            scene.createStaticMeshInstance(boxInstance.mesh(), t2);
        }
    }
    */

    scene.setAmbientIlluminance(600.0f);
    scene.setEnvironmentMap({ .assetPath = "assets/sample/hdri/tiergarten_2k.hdr",
                              .brightnessFactor = 5000.0f });

    vec3 sunDirecton = normalize(vec3(-1.0f, -1.0f, -1.0f));
    scene.addLight(std::make_unique<DirectionalLight>(vec3(1.0f), 90'000.0f, sunDirecton));

    Camera& camera = scene.addCamera("default", true);
    camera.lookAt(vec3(0, 1.0f, 4.0f), vec3(0, 0, 0));
    camera.setManualExposureParameters(11.0f, 1.0f / 125.0f, 400.0f);
    m_fpsCameraController.takeControlOfCamera(camera);

    ////////////////////////////////////////////////////////////////////////////
    // Render pipeline setup

    pipeline.addNode<PrepassNode>();
    pipeline.addNode<ForwardRenderNode>();
    // TODO: Maybe add some IBL for this?
    pipeline.addNode<SkyViewNode>();

    pipeline.addNode<TonemapNode>("SceneColor");
    pipeline.addNode<TAANode>(scene.camera());

    pipeline.addNode<DebugDrawNode>();
    
    FinalNode& finalNode = pipeline.addNode<FinalNode>("SceneColorLDR");
    finalNode.setRenderFilmGrain(false);
    finalNode.setRenderVignette(false);
}

bool MeshViewerApp::update(Scene& scene, float elapsedTime, float deltaTime)
{
    drawMenuBar();

    ImGuiID dockspace = ImGui::DockSpaceOverViewport(nullptr, ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoDockingInCentralNode);

    drawMeshHierarchyPanel();
    drawMeshPhysicsPanel();
    drawMeshMaterialPanel();

    //ImGui::DockBuilderSplitNode(dockspace, ImGuiDir_Left, 0.2f, nullptr, &dockspace_id);
    //ImGui::SetNextWindowDockID(dockspace, ImGuiCond_Always);

    m_fpsCameraController.update(Input::instance(), deltaTime);
    return true;
}

void MeshViewerApp::drawMenuBar()
{
    static bool showGpuSceneGui = false;
    if (showGpuSceneGui) {
        if (ImGui::Begin("GPU scene stats", &showGpuSceneGui, ImGuiWindowFlags_NoCollapse)) {
            m_scene->gpuScene().drawStatsGui();
        }
        ImGui::End();
    }

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
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Import")) {
            if (ImGui::MenuItem("Import meshes...")) {
                importMeshWithDialog();
            }
            if (ImGui::MenuItem("Import level...")) {
                importLevelWithDialog();
            }
            ImGui::Separator();
            if (ImGui::BeginMenu("Import options")) {
                ImGui::MenuItem("Always make image assets", nullptr, &m_importOptions.alwaysMakeImageAsset);
                ImGui::MenuItem("Compress images", nullptr, &m_importOptions.blockCompressImages);
                ImGui::MenuItem("Generate mipmaps", nullptr, &m_importOptions.generateMipmaps);
                ImGui::MenuItem("Save meshes as json", nullptr, &m_importOptions.saveMeshesInTextualFormat);
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Debug")) {
            ImGui::MenuItem("GPU Scene stats", nullptr, &showGpuSceneGui);
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
            m_scene->clearAllMeshInstances();
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
    if (m_targetAsset != nullptr) {

        ImGui::Checkbox("Draw bounding box", &m_drawBoundingBox);
        if (m_drawBoundingBox) {
            ark::aabb3 aabb = m_targetAsset->boundingBox;
            DebugDrawer::get().drawBox(aabb.min, aabb.max, vec3(1.0f, 1.0f, 1.0f));
        }

        if (ImGui::BeginTabBar("MeshViewerLODTabBar")) {

            for (uint32_t lodIdx = 0; lodIdx < targetAsset().LODs.size(); ++lodIdx) {
                std::string lodLabel = fmt::format("LOD{}", lodIdx);
                if (ImGui::BeginTabItem(lodLabel.c_str())) {

                    m_selectedLodIdx = lodIdx;
                    MeshLODAsset& lod = targetAsset().LODs[lodIdx];

                    if (m_selectedSegmentIdx >= lod.meshSegments.size()) {
                        m_selectedSegmentIdx = 0;
                    }

                    // Preload the cache first time around (or if the segment count is massive)..
                    // We can never have this list grow during rendering of this ImGui frame.
                    if (lod.meshSegments.size() > m_segmentNameCache.size()) {
                        size_t numSegmentNames = std::max(static_cast<size_t>(1'000), lod.meshSegments.size());
                        for (int idx = 0; idx < numSegmentNames; ++idx) {
                            m_segmentNameCache.push_back(fmt::format("segment{:03}", idx));
                        }
                    }

                    auto itemGetter = [](void* data, int idx, const char** outText) -> bool {
                        auto& segmentNameCache = *reinterpret_cast<std::vector<std::string>*>(data);
                        ARKOSE_ASSERT(idx < segmentNameCache.size());
                        *outText = segmentNameCache[idx].data();
                        return true;
                    };

                    int numSegments = static_cast<int>(lod.meshSegments.size());
                    int numToDisplay = std::min(numSegments, 15);
                    bool didClickSegment = ImGui::ListBox("Mesh segments", &m_selectedSegmentIdx, itemGetter, &m_segmentNameCache, numSegments, numToDisplay);

                    if (didClickSegment) {
                        //ARKOSE_LOG(Info, "Clicked on segment '{}'", m_segmentNameCache[m_selectedSegmentIdx]);
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
    ImGui::Begin("Material");
    if (MeshSegmentAsset* segmentAsset = selectedSegmentAsset()) {

        // Only handle non-packaged up assets here, i.e. using a path, not a direct assets as it would be in a packed case
        ARKOSE_ASSERT(segmentAsset->hasPathToMaterial());
        std::string materialPath = std::string(segmentAsset->pathToMaterial());

        ImGui::BeginDisabled();
        ImGui::InputText("Material asset", materialPath.data(), materialPath.length(), ImGuiInputTextFlags_ReadOnly);
        ImGui::EndDisabled();

        // NOTE: We're not actually loading it from disk every time because it's cached, but this still seems a little silly to do.
        if (MaterialAsset* material = MaterialAsset::load(materialPath)) {

            auto drawMaterialInputGui = [&](const char* name, std::optional<MaterialInput> materialInput) -> bool {

                bool didChange = false;

                ImGui::PushID(name);

                ImGuiTreeNodeFlags flags = 0;
                if (not materialInput.has_value()) {
                    //flags = ImGuiTreeNodeFlags_Leaf;
                    ImGui::BeginDisabled();
                }

                if (ImGui::CollapsingHeader(name, flags)) {
                    if (materialInput.has_value()) {

                        // Only handle non-packaged up assets here, i.e. using a path, not a direct assets as it would be in a packed case
                        std::string imagePath = std::string(materialInput->pathToImage());

                        ImGui::BeginDisabled();
                        ImGui::InputText("Image asset", imagePath.data(), imagePath.length(), ImGuiInputTextFlags_ReadOnly);
                        ImGui::EndDisabled();

                        didChange |= drawWrapModeSelectorGui("Wrap modes", materialInput->wrapModes);

                        didChange |= drawImageFilterSelectorGui("Mag. filter", materialInput->magFilter);
                        didChange |= drawImageFilterSelectorGui("Min. filter", materialInput->minFilter);

                        didChange |= ImGui::Checkbox("Using mip mapping", &materialInput->useMipmapping);
                        if (materialInput->useMipmapping) {
                            didChange |= drawImageFilterSelectorGui("Mipmap filter", materialInput->mipFilter);
                        }
                    }
                }

                if (not materialInput.has_value()) {
                    ImGui::EndDisabled();
                }

                ImGui::PopID();

                return didChange;
            };

            bool materialDidChange = false;

            // TODO: Add something for when we actually support multiple BRDFs..
            int currentBrdfItem = 0;
            materialDidChange |= ImGui::Combo("BRDF", &currentBrdfItem, "GGX-based microfacet model");

            materialDidChange |= drawMaterialInputGui("Base color", material->baseColor);
            materialDidChange |= drawMaterialInputGui("Emissive color", material->emissiveColor);
            materialDidChange |= drawMaterialInputGui("Normal map", material->normalMap);
            materialDidChange |= drawMaterialInputGui("Properties map", material->materialProperties);

            materialDidChange |= ImGui::ColorEdit4("Tint", value_ptr(material->colorTint));

            materialDidChange |= drawBlendModeSelectorGui("Blend mode", material->blendMode);
            if (material->blendMode == BlendMode::Masked) {
                materialDidChange |= ImGui::SliderFloat("Mask cutoff", &material->maskCutoff, 0.0f, 1.0f);
            }

            if (materialDidChange) {
                if (StaticMeshSegment* segment = selectedSegment()) {
                    MaterialHandle oldMaterial = segment->material;
                    segment->material = m_scene->gpuScene().registerMaterial(material);
                    m_scene->gpuScene().unregisterMaterial(oldMaterial);
                }
            }
        }
    }
    ImGui::End();
}

bool MeshViewerApp::drawWrapModeSelectorGui(const char* id, ImageWrapModes& wrapModes)
{
    bool didChange = false;

    auto drawWrapModeComboBox = [&](const char* innerId, ImageWrapMode& wrapMode) -> bool {

        int currentWrapModeIdx = static_cast<int>(wrapMode);
        const char* currentWrapModeString = ImageWrapModeName(wrapMode);

        if (ImGui::BeginCombo(innerId, currentWrapModeString)) {

            bool valueChanged = false;

            int wrapModeMin = static_cast<int>(ImageWrapMode_Min);
            int wrapModeMax = static_cast<int>(ImageWrapMode_Max);

            for (int i = wrapModeMin; i <= wrapModeMax; i++) {
                ImGui::PushID(i);

                auto itemWrapMode = static_cast<ImageWrapMode>(i);
                const char* itemText = ImageWrapModeName(itemWrapMode);

                if (ImGui::Selectable(itemText, i == currentWrapModeIdx)) {
                    wrapMode = itemWrapMode;
                    valueChanged = true;
                }

                if (valueChanged) {
                    ImGui::SetItemDefaultFocus();
                }

                ImGui::PopID();
            }
            ImGui::EndCombo();

            return valueChanged;
        }

        return false;
    };

    // TODO: Fix layout!
    if (ImGui::BeginTable(id, 4, ImGuiTableFlags_NoBordersInBody)) {

        ImGui::TableNextColumn();
        didChange |= drawWrapModeComboBox("##WrapModeComboBoxU", wrapModes.u);

        ImGui::TableNextColumn();
        didChange |= drawWrapModeComboBox("##WrapModeComboBoxV", wrapModes.v);

        ImGui::TableNextColumn();
        didChange |= drawWrapModeComboBox("##WrapModeComboBoxW", wrapModes.w);

        ImGui::TableNextColumn();
        ImGui::Text("Wrap mode");

        ImGui::EndTable();
    }

    return didChange;
}

bool MeshViewerApp::drawBlendModeSelectorGui(const char* id, BlendMode& blendMode)
{
    int currentBlendModeIdx = static_cast<int>(blendMode);
    const char* currentBlendModeString = BlendModeName(blendMode);

    if (ImGui::BeginCombo(id, currentBlendModeString)) {

        bool valueChanged = false;

        int blendModeMin = static_cast<int>(BlendMode_Min);
        int blendModeMax = static_cast<int>(BlendMode_Max);

        for (int i = blendModeMin; i <= blendModeMax; i++) {
            ImGui::PushID(i);

            auto itemBlendMode = static_cast<BlendMode>(i);
            const char* itemText = BlendModeName(itemBlendMode);

            if (ImGui::Selectable(itemText, i == currentBlendModeIdx)) {
                blendMode = itemBlendMode;
                valueChanged = true;
            }

            if (valueChanged) {
                ImGui::SetItemDefaultFocus();
            }

            ImGui::PopID();
        }
        ImGui::EndCombo();

        return valueChanged;
    }

    return false;
}

bool MeshViewerApp::drawImageFilterSelectorGui(const char* id, ImageFilter& imageFilter)
{
    bool didChange = false;

    int currentImageFilterIdx = static_cast<int>(imageFilter);
    const char* currentImageFilterString = ImageFilterName(imageFilter);

    if (ImGui::BeginCombo(id, currentImageFilterString)) {

        bool valueChanged = false;

        int imageFilterMin = static_cast<int>(ImageFilter_Min);
        int imageFilterMax = static_cast<int>(ImageFilter_Max);

        for (int i = imageFilterMin; i <= imageFilterMax; i++) {
            ImGui::PushID(i);

            auto itemImageFilter = static_cast<ImageFilter>(i);
            const char* itemText = ImageFilterName(itemImageFilter);

            if (ImGui::Selectable(itemText, i == currentImageFilterIdx)) {
                imageFilter = itemImageFilter;
                valueChanged = true;
            }

            if (valueChanged) {
                ImGui::SetItemDefaultFocus();
                didChange = true;
            }

            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

    return didChange;
}

void MeshViewerApp::drawMeshPhysicsPanel()
{
    ImGui::Begin("Physics");
    if (m_targetAsset != nullptr) {
        if (ImGui::BeginTabBar("PhysicsTabBar")) {
            
            if (ImGui::BeginTabItem("Simple physics")) {
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Complex physics")) {
                if (ImGui::Button("Generate complex physics from mesh")) {

                    constexpr int lodForPhysics = 0;
                    std::vector<PhysicsMesh> physicsMeshes = m_targetAsset->createPhysicsMeshes(lodForPhysics);
                    PhysicsShapeHandle shapeHandle = m_scene->physicsScene().backend().createPhysicsShapeForTriangleMeshes(physicsMeshes);

                }
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

void MeshViewerApp::importMeshWithDialog()
{
    std::vector<FileDialog::FilterItem> filterItems = { { "glTF", "gltf,glb" } };

    if (auto maybePath = FileDialog::open(filterItems)) {

        std::string importFilePath = maybePath.value();
        ARKOSE_LOG(Info, "Importing mesh from file '{}'", importFilePath);

        std::string_view importFileDir = FileIO::extractDirectoryFromPath(importFilePath);
        std::string targetDirectory = FileIO::normalizePath(importFileDir);

        AssetImporter importer {};
        ImportResult assets = importer.importAsset(importFilePath, targetDirectory, m_importOptions);

        ARKOSE_LOG(Info, "Imported {} meshes, {} materials, and {} images.",
                   assets.meshes.size(), assets.materials.size(), assets.images.size());
    }
}


void MeshViewerApp::importLevelWithDialog()
{
    std::vector<FileDialog::FilterItem> filterItems = { { "glTF", "gltf,glb" } };

    if (auto maybePath = FileDialog::open(filterItems)) {

        std::string importFilePath = maybePath.value();
        ARKOSE_LOG(Info, "Importing level from file '{}'", importFilePath);

        std::string_view importFileDir = FileIO::extractDirectoryFromPath(importFilePath);
        std::string targetDirectory = FileIO::normalizePath(importFileDir);

        AssetImporter importer {};
        std::unique_ptr<LevelAsset> levelAsset = importer.importAsLevel(importFilePath, targetDirectory, m_importOptions);

        ARKOSE_LOG(Info, "Imported level.");
    }
}

void MeshViewerApp::loadMeshWithDialog()
{
    if (auto maybePath = FileDialog::open({ { "Arkose mesh", MeshAsset::AssetFileExtension } })) {

        std::string openPath = maybePath.value();
        ARKOSE_LOG(Info, "Loading mesh from file '{}'", openPath);

        if (MeshAsset* meshAsset = MeshAsset::load(openPath)) {

            m_scene->clearAllMeshInstances();

            m_targetAsset = meshAsset;
            m_targetInstance = &m_scene->addMesh(meshAsset);
        }
    }
}

void MeshViewerApp::saveMeshWithDialog()
{
    if (auto maybePath = FileDialog::save({ { "Arkose mesh", MeshAsset::AssetFileExtension } })) {
        
        std::string savePath = maybePath.value();
        ARKOSE_LOG(Info, "Saving mesh to file '{}'", savePath);
        // TODO: Save all(?) m_targets to savePath

    }
}

StaticMeshLOD* MeshViewerApp::selectedLOD()
{
    if (m_targetInstance) {
        if (StaticMesh* staticMesh = m_scene->gpuScene().staticMeshForHandle(m_targetInstance->mesh())) {
            return &staticMesh->LODs()[m_selectedLodIdx];
        }
    }

    return nullptr;
}

StaticMeshSegment* MeshViewerApp::selectedSegment()
{
    if (StaticMeshLOD* lod = selectedLOD()) {
        return &lod->meshSegments[m_selectedSegmentIdx];
    }

    return nullptr;
}
