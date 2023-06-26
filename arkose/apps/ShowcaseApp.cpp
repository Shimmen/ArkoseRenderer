#include "ShowcaseApp.h"

#include "rendering/meshlet/MeshletDebugNode.h"
#include "rendering/meshlet/MeshletForwardRenderNode.h"
#include "rendering/meshlet/MeshletVisibilityBufferRenderNode.h"
#include "rendering/meshlet/VisibilityBufferDebugNode.h"
#include "rendering/nodes/BloomNode.h"
#include "rendering/nodes/CullingNode.h"
#include "rendering/nodes/DDGINode.h"
#include "rendering/nodes/DDGIProbeDebug.h"
#include "rendering/nodes/DebugDrawNode.h"
#include "rendering/nodes/DepthOfFieldNode.h"
#include "rendering/nodes/DirectionalLightShadowNode.h"
#include "rendering/nodes/FXAANode.h"
#include "rendering/nodes/FinalNode.h"
#include "rendering/nodes/ForwardRenderNode.h"
#include "rendering/nodes/GIComposeNode.h"
#include "rendering/nodes/LocalLightShadowNode.h"
#include "rendering/nodes/PickingNode.h"
#include "rendering/nodes/PrepassNode.h"
#include "rendering/nodes/RTSphereLightShadowNode.h"
#include "rendering/nodes/RTReflectionsNode.h"
#include "rendering/nodes/RTVisualisationNode.h"
#include "rendering/nodes/SSAONode.h"
#include "rendering/nodes/SkyViewNode.h"
#include "rendering/nodes/TAANode.h"
#include "rendering/nodes/TonemapNode.h"
#include "rendering/nodes/TranslucencyNode.h"
#include "rendering/nodes/VisibilityBufferShadingNode.h"
#include "scene/Scene.h"
#include "scene/camera/Camera.h"
#include "scene/lights/DirectionalLight.h"
#include "utility/Input.h"
#include "utility/Profiling.h"
#include <ark/random.h>
#include <imgui.h>

// For physics experimenting, to be removed / moved into the scene!
#include "physics/PhysicsScene.h"
#include "physics/backend/base/PhysicsBackend.h"

// For animation & skinning tests
#include "asset/import/AssetImporter.h"

constexpr bool keepRenderDocCompatible = false;
constexpr bool withRayTracing = true && !keepRenderDocCompatible;
constexpr bool withMeshShading = true && !keepRenderDocCompatible;
constexpr bool withVisibilityBuffer = true && withMeshShading;

std::vector<Backend::Capability> ShowcaseApp::requiredCapabilities()
{
    std::vector<Backend::Capability> capabilities {};

    if constexpr (withRayTracing) {
        capabilities.push_back(Backend::Capability::RayTracing);
    }

    if constexpr (withMeshShading) {
        capabilities.push_back(Backend::Capability::MeshShading);
    }

    return capabilities;
}

void ShowcaseApp::setup(Scene& scene, RenderPipeline& pipeline)
{
    SCOPED_PROFILE_ZONE();

    Scene::Description description { .maintainRayTracingScene = withRayTracing,
                                     .meshShadingCapable = withMeshShading };
    // NOTE: Scene not under "assets/sample/" will not be available in the Git-repo, either due to file size or license or both!
    //description.path = "assets/IntelSponza/NewSponzaWithCurtains.arklvl";
    //description.path = "assets/PicaPica/PicaPicaMiniDiorama.arklvl";
    description.path = "assets/sample/Sponza.arklvl";
    scene.setupFromDescription(description);

    if (description.path.empty()) {
        //setupCullingShowcaseScene(scene);

        AssetImporter importer {};
        importer.importAsLevel("assets/sample/models/RiggedSimple/RiggedSimple.gltf",
                               "assets/sample/models/RiggedSimple/",
                               AssetImporter::Options());


        MeshAsset* cylinderMeshAsset = MeshAsset::load("assets/sample/models/RiggedSimple/Cylinder.arkmsh");
        SkeletonAsset* cylinderSkeletonAsset = SkeletonAsset::load("assets/sample/models/RiggedSimple/Armature.arkskel");
        AnimationAsset* cylinderBindAnimAsset = AnimationAsset::load("assets/sample/models/RiggedSimple/animation0000.arkanim");

        Transform transform {};
        transform.setOrientation(quat(vec3(0.5f, 0.5f, 0.5f), -0.5f));
        //StaticMeshInstance& staticMeshInstance = scene.addMesh(cylinderMeshAsset, transform);
        m_skeletalMeshInstance = &scene.addSkeletalMesh(cylinderMeshAsset, cylinderSkeletonAsset, transform);

        m_testAnimation = Animation::bind(cylinderBindAnimAsset, *m_skeletalMeshInstance);

        Camera& camera = scene.addCamera("LookatCam", true);
        camera.lookAt(vec3(0.0f, 0.0f, 15.0f), vec3(0.0f, 0.0f, 0.0f));
    }

    if (scene.directionalLightCount() == 0) {
        DirectionalLight& sun = scene.addLight(std::make_unique<DirectionalLight>(vec3(1.0f), 90'000.0f, normalize(vec3(0.5f, -1.0f, 0.2f))));
        sun.transform().setTranslation({ 0.0f, 2.5f, 0.0f });
    }

    Camera& camera = scene.camera();
    m_fpsCameraController.takeControlOfCamera(camera);

    pipeline.addNode<PickingNode>();

    if constexpr (withRayTracing) {
        scene.generateProbeGridFromBoundingBox();
        pipeline.addNode<DDGINode>();
    } else {
        scene.setAmbientIlluminance(250.0f);
    }

    if constexpr (withVisibilityBuffer) {
        pipeline.addNode<MeshletVisibilityBufferRenderNode>();
    } else {
        pipeline.addNode<CullingNode>();
        pipeline.addNode<PrepassNode>(PrepassMode::AllOpaquePixels);
    }

    if constexpr (withRayTracing) {
        pipeline.addNode<RTSphereLightShadowNode>();
    }
    pipeline.addNode<DirectionalLightShadowNode>();
    pipeline.addNode<LocalLightShadowNode>();

    if constexpr (withVisibilityBuffer) {
        pipeline.addNode<VisibilityBufferShadingNode>();
    } else if constexpr (withMeshShading) {
        pipeline.addNode<MeshletForwardRenderNode>();
    } else {
        pipeline.addNode<ForwardRenderNode>();
    }

    if constexpr (withRayTracing) {
        pipeline.addNode<RTReflectionsNode>();
    }

    pipeline.addNode<SSAONode>();
    pipeline.addNode<GIComposeNode>();
    
    pipeline.addNode<SkyViewNode>();

    pipeline.addNode<TranslucencyNode>();

    //pipeline.addNode<BloomNode>();

    auto& dofNode = pipeline.addNode<DepthOfFieldNode>();
    dofNode.setEnabled(false);

    if constexpr (withRayTracing) {
        pipeline.addNode<DDGIProbeDebug>();
    }

    std::string sceneTexture = "SceneColor";
    const std::string finalTextureToScreen = "SceneColorLDR";
    const AntiAliasing antiAliasingMode = AntiAliasing::TAA;

    if constexpr (withVisibilityBuffer) {
        // Uncomment for visibility buffer visualisation
        //pipeline.addNode<VisibilityBufferDebugNode>(); sceneTexture = "VisibilityBufferDebugVis";
    }

    if constexpr (withMeshShading) {
        // Uncomment for meshlet visualisation
        //pipeline.addNode<MeshletDebugNode>(); sceneTexture = "MeshletDebugVis";
    }

    if constexpr (withRayTracing) {
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

    pipeline.addNode<DebugDrawNode>();

    FinalNode& finalNode = pipeline.addNode<FinalNode>(finalTextureToScreen);
    finalNode.setRenderFilmGrain(true);

    // Save reference to the render pipeline for GUI purposes
    m_renderPipeline = &pipeline;
}

bool ShowcaseApp::update(Scene& scene, float elapsedTime, float deltaTime)
{
    SCOPED_PROFILE_ZONE();

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

    if (DirectionalLight* sun = scene.firstDirectionalLight()) {
        sun->transform().setOrientation(rotation * sun->transform().localOrientation());
    }

    for (AnimatingInstance animatingInstance : m_animatingInstances) {
        quat rotation = axisAngle(animatingInstance.axisOfRotation, animatingInstance.rotationSpeed * deltaTime);
        quat orientiation = animatingInstance.staticMeshInstance->transform().localOrientation();
        animatingInstance.staticMeshInstance->transform().setOrientation(rotation * orientiation);
    }

    // Physics experiment, to be removed!
    if (input.wasKeyPressed(Key::T)) {

        Camera const& camera = scene.camera();
        vec3 spawnDirection = camera.forward();
        vec3 spawnPosition = camera.position() + 1.5f * spawnDirection;

        constexpr float scale = 0.25f;
        Transform xform { spawnPosition, camera.orientation(), vec3(scale) };

        static MeshAsset* redCube = nullptr;
        static PhysicsShapeHandle cubeShapeHandle {};
        if (not redCube) {
            redCube = MeshAsset::load("assets/sample/models/Box/Box.arkmsh");

            vec3 scaledHalfExtent = 0.5f * (redCube->boundingBox.max - redCube->boundingBox.min) * scale;
            cubeShapeHandle = scene.physicsScene().backend().createPhysicsShapeForBox(scaledHalfExtent);
        }

        StaticMeshInstance& staticMeshInstance = scene.addMesh(redCube, xform);
        PhysicsInstanceHandle physicsInstanceHandle = scene.physicsScene().createDynamicInstance(cubeShapeHandle, staticMeshInstance.transform());
        scene.physicsScene().backend().applyImpulse(physicsInstanceHandle, 175.0f * spawnDirection);
    }

    if (m_testAnimation != nullptr) {
        if (input.wasKeyPressed(Key::R)) {
            fmt::print("\n\nReseting animation\n\n");
            m_testAnimation->reset();
        }

        m_skeletalMeshInstance->skeleton().debugPrintState();
        m_testAnimation->tick(deltaTime);
    }

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

void ShowcaseApp::setupCullingShowcaseScene(Scene& scene)
{
    constexpr int NumAnimatingInstances = 4096;

    m_animatingInstances.clear();
    m_animatingInstances.reserve(NumAnimatingInstances);

    MeshAsset* helmetAsset = MeshAsset::load("assets/sample/models/DamagedHelmet/DamagedHelmet.arkmsh");
    StaticMeshHandle helmet = scene.gpuScene().registerStaticMesh(helmetAsset);

    m_fpsCameraController.setMaxSpeed(35.0f);
    ark::aabb3 spawnBox { vec3(-50.0f, -50.0f, -50.0f), vec3(+50.0f, +50.0f, +50.0f) };

    ark::Random rng { 12345 };

    for (size_t idx = 0; idx < NumAnimatingInstances; ++idx) {

        Transform transform {};
        transform.setTranslation(spawnBox.min + (rng.randomInUnitCube() + vec3(1.0f)) * spawnBox.extents());
        transform.setScale(vec3(rng.randomFloatInRange(1.0f, 10.0f)));
        transform.setOrientation(rng.randomRotation());

        StaticMeshInstance& instance = scene.createStaticMeshInstance(helmet, transform);

        AnimatingInstance animatingInstance { .staticMeshInstance = &instance,
                                              .axisOfRotation = rng.randomDirection(),
                                              .rotationSpeed = rng.randomFloatInRange(-2.5f, 2.5f) };

        m_animatingInstances.push_back(animatingInstance);

    }
}
