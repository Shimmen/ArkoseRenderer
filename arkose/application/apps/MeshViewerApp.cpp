#include "MeshViewerApp.h"

#include "asset/MaterialAsset.h"
#include "asset/import/AssetImporter.h"
#include "system/Input.h"
#include "physics/PhysicsScene.h"
#include "physics/backend/base/PhysicsBackend.h"
#include "rendering/baking/BakeAmbientOcclusionNode.h"
#include "rendering/debug/DebugDrawer.h"
#include "rendering/debug/EditorGridRenderNode.h"
#include "rendering/forward/ForwardRenderNode.h"
#include "rendering/forward/PrepassNode.h"
#include "rendering/nodes/BloomNode.h"
#include "rendering/nodes/DebugDrawNode.h"
#include "rendering/nodes/SSAONode.h"
#include "rendering/nodes/SkyViewNode.h"
#include "rendering/nodes/TAANode.h"
#include "rendering/output/OutputNode.h"
#include "scene/Scene.h"
#include "scene/camera/Camera.h"
#include "scene/lights/DirectionalLight.h"
#include "utility/FileDialog.h"
#include "utility/FileIO.h"
#include "utility/Profiling.h"
#include <fmt/format.h>
#include <imgui.h>
#include <imgui_internal.h>

#include "shaders/shared/TonemapData.h"

std::vector<Backend::Capability> MeshViewerApp::optionalCapabilities()
{
    return { Backend::Capability::RayTracing, Backend::Capability::ShaderBarycentrics };
}

void MeshViewerApp::setup(Scene& scene, RenderPipeline& pipeline)
{
    SCOPED_PROFILE_ZONE();

    ////////////////////////////////////////////////////////////////////////////
    // Scene setup
    
    m_scene = &scene;

    scene.setupFromDescription({ .withRayTracing = false,
                                 .withMeshShading = false });

    if (MeshAsset* defaultMeshAsset = MeshAsset::load("assets/sample/models/Box/Box.arkmsh")) {
        m_targetAsset = defaultMeshAsset;
        m_targetInstance = &m_scene->addMesh(defaultMeshAsset);
        m_targetInstance->transform().setOrientation(ark::axisAngle(ark::globalUp, ark::toRadians(30.0f)));
    }

    scene.setAmbientIlluminance(150.0f);
    scene.setEnvironmentMap({ .assetPath = "assets/sample/hdri/tiergarten_2k.hdr",
                              .brightnessFactor = 10000.0f });

    vec3 sunDirecton = normalize(vec3(-1.0f, -1.0f, -1.0f));
    scene.addLight(std::make_unique<DirectionalLight>(Colors::white, 90'000.0f, sunDirecton));

    Camera& camera = scene.addCamera("default", true);
    camera.lookAt(vec3(0, 1.0f, 4.0f), vec3(0, 0, 0));
    camera.setManualExposureParameters(11.0f, 1.0f / 125.0f, 100.0f);
    m_fpsCameraController.takeControlOfCamera(camera);
    m_fpsCameraController.setMaxSpeed(2.5f);

    ////////////////////////////////////////////////////////////////////////////
    // Render pipeline setup

    pipeline.addNode<ForwardRenderNode>(ForwardRenderNode::Mode::Opaque,
                                        ForwardMeshFilter::AllMeshes,
                                        ForwardClearMode ::ClearBeforeFirstDraw);

    // TODO: Maybe add some IBL for this?
    pipeline.addNode<SkyViewNode>();
    
    pipeline.addNode<ForwardRenderNode>(ForwardRenderNode::Mode::Translucent,
                                        ForwardMeshFilter::AllMeshes,
                                        ForwardClearMode ::DontClear);

    pipeline.addNode<TAANode>(scene.camera());

    OutputNode& outputNode = pipeline.addNode<OutputNode>("SceneColor");
    outputNode.setTonemapMethod(TONEMAP_METHOD_KHRONOS_PBR_NEUTRAL);
    outputNode.setRenderFilmGrain(false);
    outputNode.setRenderVignette(false);

    m_editorGrid = &pipeline.addNode<EditorGridRenderNode>();
    pipeline.addNode<DebugDrawNode>();
}

bool MeshViewerApp::update(Scene& scene, float elapsedTime, float deltaTime)
{
    drawMenuBar();

    ImGuiID dockspace = ImGui::DockSpaceOverViewport(nullptr, ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoDockingInCentralNode);
    (void)dockspace;

    drawMeshHierarchyPanel();
    drawMeshPhysicsPanel();
    drawMeshMaterialPanel();

    drawBakeUiIfActive();

    if (m_currentImportTask) {

        ImVec2 displayCenter { ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f };
        ImGui::SetNextWindowPos(displayCenter, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(500, 0), ImGuiCond_Appearing);
        ImGui::Begin("Importing asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse);

        ImGui::ProgressBar(m_currentImportTask->progress());
        ImGui::Text("%s...", m_currentImportTask->status().c_str());

        if (m_currentImportTask->isCompleted()) {
            ImportResult const& result = *m_currentImportTask->result();

            ImGui::Separator();

            ImGui::Text("Imported");
            ImGui::Text("  %d meshes", narrow_cast<i32>(result.meshes.size()));
            ImGui::Text("  %d materials", narrow_cast<i32>(result.materials.size()));
            ImGui::Text("  %d images.", narrow_cast<i32>(result.images.size()));
            ImGui::Text("  %d skeletons", narrow_cast<i32>(result.skeletons.size()));
            ImGui::Text("  %d animations", narrow_cast<i32>(result.animations.size()));

            ImGui::NewLine();

            if (ImGui::Button("Create level...", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
                if (auto maybePath = FileDialog::save({ { "Arkose level", LevelAsset::AssetFileExtension } })) {

                    std::string savePath = maybePath.value();
                    ARKOSE_LOG(Info, "Saving level to file '{}'", savePath);

                    auto levelAsset = LevelAsset::createFromAssetImportResult(result);
                    levelAsset->writeToFile(savePath, AssetStorage::Json);

                    m_currentImportTask.reset();
                }
            }

            if (ImGui::Button("Close", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
                m_currentImportTask.reset();
            }
        }

        ImGui::End();
    }

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
            bool hasActiveImportTask = m_currentImportTask != nullptr;
            if (hasActiveImportTask) {
                ImGui::BeginDisabled();
            }
            if (ImGui::MenuItem("Import asset...")) {
                importAssetWithDialog();
            }
            if (hasActiveImportTask) {
                ImGui::EndDisabled();
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
    // TODO: Make the "Hierarchy" window just be a tree of the currently loaded set,
    // and then have another window for the Mesh, just like we do for the Material etc.
    ImGui::Begin("Hierarchy");
    if (m_targetAsset && m_targetInstance) {

        MeshAsset const& meshAsset = *m_targetAsset;
        std::string meshPath = std::string(meshAsset.assetFilePath());

        ImGui::Text("%s", meshPath.c_str());
        if (ImGui::Button("Save")) {
            meshAsset.writeToFile(meshPath, AssetStorage::Json);
            // TODO: *All* references to this mesh must now reload that mesh! Is that a good behaviour?
        }
        ImGui::SameLine();
        if (ImGui::Button("Save as...")) {
            if (auto maybePath = FileDialog::save({ { "Arkose mesh", MeshAsset::AssetFileExtension } })) {
                // Write the mesh to disk
                std::string newMeshPath = maybePath.value();
                meshAsset.writeToFile(newMeshPath, AssetStorage::Json); // TODO: Use binary!
                // Then immediately load it and make it the material for this segment (all other segments still use the old one)
                if (MeshAsset* newMeshAsset = MeshAsset::load(newMeshPath)) {
                    // TODO: Also assign to the parent SetAsset if there is one (there's not one yet..)
                    m_targetAsset = newMeshAsset;
                }
            }
        }

        ImGui::Checkbox("Draw bounding box", &m_drawBoundingBox);
        if (m_drawBoundingBox) {
            m_scene->drawInstanceBoundingBox(*m_targetInstance);
        }

        // This isn't really related to the current mesh so should probably be moved to its own panel..
        bool enableGrid = m_editorGrid->enabled();
        ImGui::Checkbox("Render grid", &enableGrid);
        m_editorGrid->setEnabled(enableGrid);

        if (ImGui::BeginTabBar("MeshViewerLODTabBar")) {

            for (uint32_t lodIdx = 0; lodIdx < targetAsset().LODs.size(); ++lodIdx) {
                std::string lodLabel = fmt::format("LOD{}", lodIdx);
                if (ImGui::BeginTabItem(lodLabel.c_str())) {

                    m_selectedLodIdx = lodIdx;
                    MeshLODAsset& lod = targetAsset().LODs[lodIdx];

                    if (m_selectedSegmentIdx >= narrow_cast<int>(lod.meshSegments.size())) {
                        m_selectedSegmentIdx = 0;
                    }

                    // Preload the cache first time around (or if the segment count is massive)..
                    // We can never have this list grow during rendering of this ImGui frame.
                    if (lod.meshSegments.size() > m_segmentNameCache.size()) {
                        size_t numSegmentNames = std::max(static_cast<size_t>(1'000), lod.meshSegments.size());
                        for (size_t idx = 0; idx < numSegmentNames; ++idx) {
                            m_segmentNameCache.push_back(fmt::format("segment{:03}", idx));
                        }
                    }

                    auto itemGetter = [](void* data, int idx, const char** outText) -> bool {
                        auto& segmentNameCache = *reinterpret_cast<std::vector<std::string>*>(data);
                        ARKOSE_ASSERT(idx < narrow_cast<int>(segmentNameCache.size()));
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

        if (MeshSegmentAsset* segmentAsset = selectedSegmentAsset()) {

            if (ImGui::TreeNode("Geometry")) {
                ImGui::Text("  posititions: %u", narrow_cast<i32>(segmentAsset->positions.size()));
                ImGui::Text("    texcoords: %u", narrow_cast<i32>(segmentAsset->texcoord0s.size()));
                ImGui::Text("      normals: %u", narrow_cast<i32>(segmentAsset->normals.size()));
                ImGui::Text("     tangents: %u", narrow_cast<i32>(segmentAsset->tangents.size()));
                ImGui::Spacing();
                ImGui::Text("joint indices: %u", narrow_cast<i32>(segmentAsset->jointIndices.size()));
                ImGui::Text("joint weights: %u", narrow_cast<i32>(segmentAsset->jointWeights.size()));
                ImGui::Spacing();
                ImGui::Text("      indices: %u", narrow_cast<i32>(segmentAsset->indices.size()));

                // TODO: Add option for (re-)generating tangents here!

                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Material")) {

                // Only handle non-packaged up assets here, i.e. using a path, not a direct assets as it would be in a packed case
                ARKOSE_ASSERT(segmentAsset->hasPathToMaterial());
                std::string materialPath = std::string(segmentAsset->pathToMaterial());

                ImGui::InputText("Material", materialPath.data(), materialPath.length(), ImGuiInputTextFlags_ReadOnly);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", materialPath.c_str());
                }
                ImGui::SameLine();
                if (ImGui::Button("...")) {
                    if (auto maybePath = FileDialog::open({ { "Arkose material", MaterialAsset::AssetFileExtension } })) {
                        std::string newMaterialPath = maybePath.value();
                        if (MaterialAsset* newMaterialAsset = MaterialAsset::load(newMaterialPath)) {
                            segmentAsset->setPathToMaterial(newMaterialPath); // TODO: Avoid setting an absolute path here!
                            selectedSegment()->setMaterial(newMaterialAsset, m_scene->gpuScene());
                        }
                    }
                }
                ImGui::TreePop();
            }
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

        // NOTE: We're not actually loading it from disk every time because it's cached, but this still seems a little silly to do.
        if (MaterialAsset* material = MaterialAsset::load(materialPath)) {

            ImGui::Text("%s", materialPath.c_str());
            if (ImGui::Button("Save")) {
                material->writeToFile(materialPath, AssetStorage::Json);
                // TODO: *All* references to this material must now reload their material! Right? Is that a good behaviour?
            }
            ImGui::SameLine();
            if (ImGui::Button("Save as...")) {
                if (auto maybePath = FileDialog::save({ { "Arkose material", MaterialAsset::AssetFileExtension } })) {
                    // Write the material to disk
                    std::string newMaterialPath = maybePath.value();
                    material->writeToFile(newMaterialPath, AssetStorage::Json);
                    // Then immediately load it and make it the material for this segment (all other segments still use the old one)
                    if (MaterialAsset* newMaterialAsset = MaterialAsset::load(newMaterialPath)) {
                        segmentAsset->setPathToMaterial(newMaterialPath); // TODO: Avoid setting an absolute path here!
                        selectedSegment()->setMaterial(newMaterialAsset, m_scene->gpuScene());
                        material = newMaterialAsset;
                    }
                }
            }

            auto drawMaterialInputGui = [&](const char* name, std::optional<MaterialInput>& materialInput, int textureIndex, bool includeBakeBentNormalsUI = false) -> bool {

                bool didChange = false;

                ImGui::PushID(name);

                if (ImGui::CollapsingHeader(name)) {
                    if (materialInput.has_value()) {

                        auto imageSelectDialog = [&]() {
                            if (auto maybePath = FileDialog::open({ { "Arkose image", ImageAsset::AssetFileExtension },
                                                                    { "png", "png" },
                                                                    { "jpeg", "jpeg,jpg" } })) {
                                std::string newImagePath = maybePath.value();
                                if (ImageAsset* newImageAsset = ImageAsset::loadOrCreate(newImagePath)) {
                                    materialInput->setPathToImage(newImagePath);
                                    didChange |= true;
                                }
                            }
                        };

                        if (Texture const* texture = m_scene->gpuScene().textureForHandle(TextureHandle(textureIndex))) {
                            ImTextureID textureId = const_cast<Texture*>(texture)->asImTextureID(); // HACK: const_cast
                            if (ImGui::ImageButton(textureId, ImVec2(512.0f * texture->extent().aspectRatio(), 512.0f))) { 
                                imageSelectDialog();
                            }
                            if (ImGui::IsItemHovered()) {
                                std::string imagePath = std::string(materialInput->pathToImage());
                                ImGui::SetTooltip("%s", imagePath.c_str());
                            }
                        } else {
                            if (ImGui::Button("Add image...")) {
                                imageSelectDialog();
                            }
                        }

                        didChange |= drawWrapModeSelectorGui("Wrap modes", materialInput->wrapModes);

                        didChange |= drawImageFilterSelectorGui("Mag. filter", materialInput->magFilter);
                        didChange |= drawImageFilterSelectorGui("Min. filter", materialInput->minFilter);

                        didChange |= ImGui::Checkbox("Using mip mapping", &materialInput->useMipmapping);
                        if (materialInput->useMipmapping) {
                            didChange |= drawImageFilterSelectorGui("Mipmap filter", materialInput->mipFilter);
                        }
                    } else {
                        if (ImGui::Button("Add input")) { 
                            materialInput = MaterialInput();
                        }
                        if (includeBakeBentNormalsUI && selectedSegmentAsset() && selectedSegmentAsset()->hasTextureCoordinates()) {
                            ImGui::SameLine();
                            if (ImGui::Button("Bake...")) {
                                m_pendingBake = BakeMode::BentNormals;
                            }
                        }
                    }
                }

                ImGui::PopID();

                return didChange;
            };

            bool materialDidChange = false;

            materialDidChange |= drawBrdfSelectorGui("Blend mode", material->brdf);

            ImGui::Spacing();

            // No point in showing the texture material inputs when there's no texture coordinates..
            bool showMaterialInputTextureUI = segmentAsset->hasTextureCoordinates();

            if (!showMaterialInputTextureUI) {
                ImGui::BeginDisabled();
                ImGui::Text("No texture coordinates for this mesh segment - hiding material inputs");
            }

            ShaderMaterial const* shaderMaterial = m_scene->gpuScene().materialForHandle(selectedSegment()->material);
            materialDidChange |= drawMaterialInputGui("Base color", material->baseColor, shaderMaterial->baseColor);
            materialDidChange |= drawMaterialInputGui("Emissive color", material->emissiveColor, shaderMaterial->emissive);
            materialDidChange |= drawMaterialInputGui("Normal map", material->normalMap, shaderMaterial->normalMap);
            materialDidChange |= drawMaterialInputGui("Bent normal map", material->bentNormalMap, shaderMaterial->bentNormalMap, true);
            materialDidChange |= drawMaterialInputGui("Properties map", material->materialProperties, shaderMaterial->metallicRoughness);

            if (!showMaterialInputTextureUI) {
                ImGui::EndDisabled();
            }

            ImGui::Spacing();

            materialDidChange |= ImGui::ColorEdit4("Tint", value_ptr(material->colorTint));

            materialDidChange |= drawBlendModeSelectorGui("Blend mode", material->blendMode);
            if (material->blendMode == BlendMode::Masked) {
                materialDidChange |= ImGui::SliderFloat("Mask cutoff", &material->maskCutoff, 0.0f, 1.0f);
            } else if (material->blendMode == BlendMode::Translucent) {
                materialDidChange |= ImGui::SliderFloat("Opacity (tint)", &material->colorTint.w, 0.0f, 1.0f);
            }

            if (materialDidChange) {
                selectedSegment()->setMaterial(material, m_scene->gpuScene());
            }
        }
    }
    ImGui::End();
}

bool MeshViewerApp::drawBrdfSelectorGui(const char* id, Brdf& brdf)
{
    bool didChange = false;

    int currentBrdfIdx = static_cast<int>(brdf);
    const char* currentBrdfString = BrdfName(brdf);

    if (ImGui::BeginCombo("BRDF", currentBrdfString)) {

        int brdfMin = static_cast<int>(Brdf_Min);
        int brdfMax = static_cast<int>(Brdf_Max);

        for (int i = brdfMin; i <= brdfMax; i++) {
            ImGui::PushID(i);

            auto itemBrdf = static_cast<Brdf>(i);
            const char* itemText = BrdfName(itemBrdf);

            if (ImGui::Selectable(itemText, i == currentBrdfIdx)) {
                brdf = itemBrdf;
                didChange = true;
            }

            if (didChange) {
                ImGui::SetItemDefaultFocus();
            }

            ImGui::PopID();
        }

        ImGui::EndCombo();
    }

    return didChange;
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
                ImGui::Text("TODO!");
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Complex physics")) {
                if (ImGui::Button("Generate complex physics from mesh")) {

                    constexpr int lodForPhysics = 0;
                    std::vector<PhysicsMesh> physicsMeshes = m_targetAsset->createPhysicsMeshes(lodForPhysics);
                    PhysicsShapeHandle shapeHandle = m_scene->physicsScene().backend().createPhysicsShapeForTriangleMeshes(physicsMeshes);

                    // TODO: Add the shape (in Jolt's binary format) to the mesh asset

                }
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

void MeshViewerApp::importAssetWithDialog()
{
    std::vector<FileDialog::FilterItem> filterItems = { { "glTF", "gltf,glb" } };

    if (auto maybePath = FileDialog::open(filterItems)) {

        std::string importFilePath = maybePath.value();
        ARKOSE_LOG(Info, "Importing mesh from file '{}'", importFilePath);

        std::string_view importFileDir = FileIO::extractDirectoryFromPath(importFilePath);
        std::string targetDirectory = FileIO::normalizePath(importFileDir);

        m_currentImportTask = AssetImportTask::create(importFilePath, targetDirectory, m_importOptions);
        TaskGraph::get().scheduleTask(*m_currentImportTask);
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

void MeshViewerApp::drawBakeUiIfActive()
{
    // All our baking expects texture coordinates
    if (!selectedSegmentAsset() || !selectedSegmentAsset()->hasTextureCoordinates()) {
        return;
    }
    
    // All our baking capabilities are dependent on ray tracing & reading shader barycentrics
    if (!Backend::get().hasActiveCapability(Backend::Capability::RayTracing) || !Backend::get().hasActiveCapability(Backend::Capability::ShaderBarycentrics)) {
        return;
    }

    if (m_pendingBake != BakeMode::None) {
        ImGui::OpenPopup("Bake");
    }

    if (ImGui::BeginPopupModal("Bake")) {

        static int resolutionPower = 12;
        u32 resolution = static_cast<u32>(std::pow(2.0f, resolutionPower));
        std::string resFormatString = fmt::format("{0}x{0}", resolution);
        ImGui::SliderInt("Resolution", &resolutionPower, 8, 14, resFormatString.c_str());

        static int sampleCount = 500;
        ImGui::SliderInt("Sample count", &sampleCount, 10, 1000);

        if (ImGui::Button("Bake")) {
            auto aoImage = performAmbientOcclusionBake(m_pendingBake, resolution, sampleCount);
            m_pendingBake = BakeMode::None;

            std::string_view materialDirectory = FileIO::extractDirectoryFromPath(selectedSegmentAsset()->pathToMaterial());
            if (auto maybePath = FileDialog::save({ { "Arkose image", ImageAsset::AssetFileExtension } }, materialDirectory, "AmbientOcclusion.arkimg")) {
                aoImage->writeToFile(maybePath.value(), AssetStorage::Binary);
                aoImage->setAssetFilePath(maybePath.value());

                // Let's hope no other object is using this material, because now we're saving object-specific data to it :)
                // Really though, this should only be done for non-trimsheet-style materials, but for object specific ones.
                if (MaterialAsset* material = MaterialAsset::load(std::string(selectedSegmentAsset()->pathToMaterial()))) {
                    material->bentNormalMap = MaterialInput(aoImage->assetFilePath());
                    material->bentNormalMap->wrapModes = ImageWrapModes::clampAllToEdge();
                    material->writeToFile(material->assetFilePath(), AssetStorage::Json);
                    // Re-register the material for the segment
                    selectedSegment()->setMaterial(material, m_scene->gpuScene());
                }
            }

            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

std::unique_ptr<ImageAsset> MeshViewerApp::performAmbientOcclusionBake(BakeMode bakeMode, u32 resolution, u32 sampleCount)
{
    SCOPED_PROFILE_ZONE();

    Extent2D aoTextureExtent { resolution, resolution };

    Backend& backend = Backend::get();

    auto bakeScene = std::make_unique<Scene>(backend, nullptr);
    bakeScene->setupFromDescription({ .withRayTracing = true });

    // Put the currently viewed mesh into the baking scene
    StaticMeshInstance& instanceToBake = bakeScene->addMesh(m_targetAsset);
    u32 instanceMeshLod = m_selectedLodIdx;
    u32 instanceMeshSegment = m_selectedSegmentIdx;

    auto bakePipeline = std::make_unique<RenderPipeline>(&bakeScene->gpuScene());
    bakePipeline->setOutputResolution(aoTextureExtent); // TODO: Setting this shouldn't be strictly required..? The output texture defines the output res.

    bakePipeline->addNode<BakeAmbientOcclusionNode>(instanceToBake, instanceMeshLod, instanceMeshSegment, sampleCount);

    Texture::Description outputTextureDesc { .extent = { aoTextureExtent, 1 } };
    switch (bakeMode) {
    case BakeMode::AmbientOcclusion:
        outputTextureDesc.format = Texture::Format::R8Uint;
        break;
    case BakeMode::BentNormals:
        outputTextureDesc.format = Texture::Format::RGBA8; // TODO: Probably use a higher-precision format!
        break;
    default:
        ARKOSE_LOG(Fatal, "Mesh viewer: unknown bake mode ({})", static_cast<u32>(bakeMode));
    }
    auto aoOutputTexture = backend.createTexture(outputTextureDesc);

    auto registry = std::make_unique<Registry>(backend, aoOutputTexture.get(), nullptr);
    bakePipeline->constructAll(*registry);

    auto uploadBuffer = std::make_unique<UploadBuffer>(backend, 100 * 1024 * 1024);

    std::optional<Backend::SubmitStatus> submitStatus = backend.submitRenderPipeline(*bakePipeline, *registry, *uploadBuffer, "AO Bake");
    if (!submitStatus.has_value()) {
        ARKOSE_LOG(Error, "Failed to submit AO bake");
        return nullptr;
    }

    backend.waitForSubmissionCompletion(submitStatus.value(), UINT64_MAX);

    return aoOutputTexture->copyDataToImageAsset(0);
}
