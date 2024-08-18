#include "Scene.h"

#include "asset/LevelAsset.h"
#include "asset/MeshAsset.h"
#include "asset/external/CubeLUT.h"
#include "core/Assert.h"
#include "system/Input.h"
#include "rendering/GpuScene.h"
#include "rendering/debug/DebugDrawer.h"
#include "scene/camera/Camera.h"
#include "physics/PhysicsMesh.h"
#include "physics/PhysicsScene.h"
#include "physics/backend/base/PhysicsBackend.h"
#include "utility/FileIO.h"
#include <imgui.h>
#include <ImGuizmo.h>
#include <fstream>

Scene::Scene(Backend& backend, PhysicsBackend* physicsBackend)
{
    m_gpuScene = std::make_unique<GpuScene>(*this, backend);

    if (physicsBackend != nullptr) {
        m_physicsScene = std::make_unique<PhysicsScene>(*this, *physicsBackend);
    }
}

Scene::~Scene()
{
}

void Scene::update(float elapsedTime, float deltaTime)
{
    SCOPED_PROFILE_ZONE();

    for (auto& skeletalMeshInstance : gpuScene().skeletalMeshInstances()) {
        skeletalMeshInstance->skeleton().applyJointTransformations();
    }

    if (Input::instance().wasKeyReleased(Key::Escape)) {
        clearSelectedObject();
    }

    drawSceneGizmos();

    if (hasPhysicsScene()) {
        physicsScene().commitInstancesAwaitingAdd();
    }
}

void Scene::preRender()
{
    SCOPED_PROFILE_ZONE();

    gpuScene().preRender();
    camera().preRender({});
}

void Scene::postRender()
{
    SCOPED_PROFILE_ZONE();

    gpuScene().postRender();
    camera().postRender({});
}

void Scene::setupFromDescription(const Description& description)
{
    // NOTE: Must initialize GPU scene before we start registering meshes etc.
    gpuScene().initialize({}, description.withRayTracing, description.withMeshShading);

    if (description.path.size() > 0) {
        if (FileIO::isFileReadable(description.path)) {
            if (LevelAsset* levelAsset = LevelAsset::load(description.path)) {
                addLevel(levelAsset);
            }
        } else {
            ARKOSE_ERROR("Failed to setup scene from description file '{}'", description.path);
        }
    }

    if (m_currentMainCamera == nullptr) {
        addCamera("DefaultCamera", true);
    }
}

/*
std::unique_ptr<LevelAsset> Scene::exportAsLevelAsset() const
{
    auto levelAsset = std::make_unique<LevelAsset>();
    levelAsset->name = "ExportedLevel";

    auto translateTransform = [](Transform const& xform) -> std::unique_ptr<Arkose::Asset::TransformT> {
        auto result = std::make_unique<Arkose::Asset::TransformT>();
        result->position = Arkose::Asset::Vec3(xform.positionInWorld().x,
                                               xform.positionInWorld().y,
                                               xform.positionInWorld().z);
        result->orientation = Arkose::Asset::Quat(xform.orientationInWorld().vec.x,
                                                  xform.orientationInWorld().vec.y,
                                                  xform.orientationInWorld().vec.z,
                                                  xform.orientationInWorld().w);
        result->scale = Arkose::Asset::Vec3(xform.localScale().x,
                                            xform.localScale().y,
                                            xform.localScale().z);
        return result;
    };

    for (auto& staticMeshInstance : m_staticMeshInstances) {
        Arkose::Asset::SceneObjectT& sceneObject = *levelAsset->objects.emplace_back(std::make_unique<Arkose::Asset::SceneObjectT>());

        StaticMesh const* staticMesh = gpuScene().staticMeshForHandle(staticMeshInstance->mesh);
        MeshAsset const* meshAsset = staticMesh->asset();

        sceneObject.name = staticMeshInstance->name;
        sceneObject.transform = translateTransform(staticMeshInstance->transform);

        Arkose::Asset::PathT path;
        path.path = "placeholder-path"; // meshAsset->assetFilePath(); TODO!
        sceneObject.mesh_asset.Set(path);
    }

    for (auto const& dirLight : m_directionalLights) {
        DirectionalLightAsset dirLightAsset {};
        
        dirLightAsset.name = dirLight->name();

        ark::quat orientation = ark::lookRotation(dirLight->forwardDirection(), ark::globalForward);
        Transform transform { dirLight->position(), orientation };
        dirLightAsset.transform = translateTransform(transform);

        dirLightAsset.color = Arkose::Asset::Vec3(dirLight->color.x,
                                                  dirLight->color.y,
                                                  dirLight->color.z);
        dirLightAsset.illuminance = dirLight->illuminance;

        dirLightAsset.world_extent = dirLight->shadowMapWorldExtent;
        
        LightAsset& lightAsset = levelAsset->lights.emplace_back();
        lightAsset.Set(dirLightAsset);
    }

    for (auto const& spotLight : m_spotLights) {
        SpotLightAsset spotLightAsset {};

        spotLightAsset.name = spotLight->name();

        ark::quat orientation = ark::lookRotation(spotLight->forwardDirection(), ark::globalForward);
        Transform transform { spotLight->position(), orientation };
        spotLightAsset.transform = translateTransform(transform);

        spotLightAsset.color = Arkose::Asset::Vec3(spotLight->color.x,
                                                   spotLight->color.y,
                                                   spotLight->color.z);
        spotLightAsset.luminous_intensity = spotLight->luminousIntensity;

        Arkose::Asset::PathT path;
        path.path = spotLight->iesProfile().path();
        spotLightAsset.ies_profile.Set(path);

        LightAsset& lightAsset = levelAsset->lights.emplace_back();
        lightAsset.Set(spotLightAsset);
    }

    for (auto const& [name, camera] : m_allCameras) {
        CameraAsset& cameraAsset = *levelAsset->cameras.emplace_back(std::make_unique<CameraAsset>());

        cameraAsset.name = name;

        Transform transform { camera->position(), camera->orientation() };
        cameraAsset.transform = translateTransform(transform);

        // NOTE: Assumes manual exposure
        Arkose::Asset::ManualExposureT manualExposure {};
        manualExposure.f_number = camera->fNumber();
        manualExposure.iso = camera->ISO();
        manualExposure.shutter_speed = camera->shutterSpeed();
        cameraAsset.exposure.Set(manualExposure);
    }

    {
        levelAsset->environment_map = std::make_unique<EnvironmentMapAsset>();
        levelAsset->environment_map->brightness_factor = environmentMap().brightnessFactor;
        
        Arkose::Asset::PathT path;
        path.path = environmentMap().assetPath;
        levelAsset->environment_map->image.Set(path);
    }

    if (hasProbeGrid()) {
        levelAsset->probe_grid = std::make_unique<ProbeGridAsset>();
        levelAsset->probe_grid->dimensions = Arkose::Asset::Vec3(static_cast<float>(probeGrid().gridDimensions.width()),
                                                                 static_cast<float>(probeGrid().gridDimensions.height()),
                                                                 static_cast<float>(probeGrid().gridDimensions.depth()));
        levelAsset->probe_grid->spacing = Arkose::Asset::Vec3(probeGrid().probeSpacing.x,
                                                              probeGrid().probeSpacing.y,
                                                              probeGrid().probeSpacing.z);
        levelAsset->probe_grid->offset = Arkose::Asset::Vec3(probeGrid().offsetToFirst.x,
                                                             probeGrid().offsetToFirst.y,
                                                             probeGrid().offsetToFirst.z);
    }

    return levelAsset;
}
*/

void Scene::addLevel(LevelAsset* levelAsset)
{
    SCOPED_PROFILE_ZONE();

    for (SceneObjectAsset const& sceneObjectAsset : levelAsset->objects) {

        // TODO: Handle non-path indirection
        std::string const& meshAssetPath = std::string(sceneObjectAsset.pathToMesh());
        MeshAsset* meshAsset = MeshAsset::load(meshAssetPath);

        StaticMeshInstance& instance = addMesh(meshAsset, sceneObjectAsset.transform);
        instance.name = sceneObjectAsset.name;

    }

    for (LightAsset const& lightAsset : levelAsset->lights) {
        if (lightAsset.type == "DirectionalLight") {
            addLight(std::make_unique<DirectionalLight>(lightAsset));
        } else if (lightAsset.type == "SphereLight") {
            addLight(std::make_unique<SphereLight>(lightAsset));
        } else if (lightAsset.type == "SpotLight") {
            addLight(std::make_unique<SpotLight>(lightAsset));
        } else {
            ARKOSE_LOG(Error, "Unknown light type '{}', ignoring", lightAsset.type);
        }
    }

    for (CameraAsset const& cameraAsset : levelAsset->cameras) {
        Camera& camera = addCamera("CameraName", false);
        camera.setupFromCameraAsset(cameraAsset);
    }

    if (levelAsset->environmentMap.has_value()) {
        EnvironmentMap const& environmentMap = levelAsset->environmentMap.value();
        setEnvironmentMap({ .assetPath = environmentMap.assetPath,
                            .brightnessFactor = environmentMap.brightnessFactor });
    }

    if (levelAsset->probeGrid.has_value()) {
        setProbeGrid(levelAsset->probeGrid.value());
    }
}

Camera& Scene::addCamera(const std::string& name, bool makeDefault)
{
    m_allCameras[name] = std::make_unique<Camera>();

    if (makeDefault || m_currentMainCamera == nullptr) {
        m_currentMainCamera = m_allCameras[name].get();
    }

    return *m_allCameras[name];
}

SkeletalMeshInstance& Scene::addSkeletalMesh(MeshAsset* meshAsset, SkeletonAsset* skeletonAsset, Transform transform)
{
    ARKOSE_ASSERT(meshAsset != nullptr);
    ARKOSE_ASSERT(skeletonAsset != nullptr);

    SkeletalMeshHandle skeletalMeshHandle = gpuScene().registerSkeletalMesh(meshAsset, skeletonAsset);
    SkeletalMeshInstance& instance = createSkeletalMeshInstance(skeletalMeshHandle, transform);

    return instance;
}

SkeletalMeshInstance& Scene::createSkeletalMeshInstance(SkeletalMeshHandle skeletalMeshHandle, Transform transform)
{
    SkeletalMeshInstance& instance = gpuScene().createSkeletalMeshInstance(skeletalMeshHandle, transform);

    if (hasPhysicsScene()) {
        // TODO!
    }

    return instance;
}

StaticMeshInstance& Scene::addMesh(MeshAsset* meshAsset, Transform transform)
{
    ARKOSE_ASSERT(meshAsset != nullptr);

    StaticMeshHandle staticMeshHandle = gpuScene().registerStaticMesh(meshAsset);
    StaticMeshInstance& instance = createStaticMeshInstance(staticMeshHandle, transform);

    return instance;
}

StaticMeshInstance& Scene::createStaticMeshInstance(StaticMeshHandle staticMeshHandle, Transform transform)
{
    StaticMeshInstance& instance = gpuScene().createStaticMeshInstance(staticMeshHandle, transform);

    if (hasPhysicsScene()) {
        // TODO!
    }

    return instance;
}

void Scene::clearAllMeshInstances()
{
    gpuScene().clearAllMeshInstances();
}

void Scene::addLight(std::unique_ptr<Light> light)
{
    ARKOSE_ASSERT(light);

    switch (light->type()) {

    case Light::Type::DirectionalLight: {
        DirectionalLight* directionalLightPtr = static_cast<DirectionalLight*>(light.release());
        addLight(std::unique_ptr<DirectionalLight>(directionalLightPtr));
    } break;

    case Light::Type::SpotLight: {
        SpotLight* spotLightPtr = static_cast<SpotLight*>(light.release());
        addLight(std::unique_ptr<SpotLight>(spotLightPtr));
    } break;

    case Light::Type::SphereLight: {
        SphereLight* sphereLightPtr = static_cast<SphereLight*>(light.release());
        addLight(std::unique_ptr<SphereLight>(sphereLightPtr));
    } break;

    default:
        ASSERT_NOT_REACHED();
        break;
    }
}

DirectionalLight& Scene::addLight(std::unique_ptr<DirectionalLight> light)
{
    ARKOSE_ASSERT(light);
    m_directionalLights.push_back(std::move(light));
    DirectionalLight& addedLight = *m_directionalLights.back();
    gpuScene().registerLight(addedLight);
    return addedLight;
}

SphereLight& Scene::addLight(std::unique_ptr<SphereLight> light)
{
    ARKOSE_ASSERT(light);
    m_sphereLights.push_back(std::move(light));
    SphereLight& addedLight = *m_sphereLights.back();
    gpuScene().registerLight(addedLight);
    return addedLight;
}

SpotLight& Scene::addLight(std::unique_ptr<SpotLight> light)
{
    ARKOSE_ASSERT(light);
    m_spotLights.push_back(std::move(light));
    SpotLight& addedLight = *m_spotLights.back();
    gpuScene().registerLight(addedLight);
    return addedLight;
}

DirectionalLight* Scene::firstDirectionalLight()
{
    if (m_directionalLights.size() > 0)
        return m_directionalLights.front().get();
    return nullptr;
}

size_t Scene::forEachLight(std::function<void(size_t, const Light&)> callback) const
{
    size_t nextIndex = 0;
    for (const auto& light : m_directionalLights) {
        callback(nextIndex++, *light);
    }
    for (auto& light : m_sphereLights) {
        callback(nextIndex++, *light);
    }
    for (const auto& light : m_spotLights) {
        callback(nextIndex++, *light);
    }
    return nextIndex;
}

size_t Scene::forEachLight(std::function<void(size_t, Light&)> callback)
{
    size_t nextIndex = 0;
    for (auto& light : m_directionalLights) {
        callback(nextIndex++, *light);
    }
    for (auto& light : m_sphereLights) {
        callback(nextIndex++, *light);
    }
    for (auto& light : m_spotLights) {
        callback(nextIndex++, *light);
    }
    return nextIndex;
}

void Scene::setEnvironmentMap(EnvironmentMap environmentMap)
{
    if (m_environmentMap.assetPath != environmentMap.assetPath) {
        gpuScene().updateEnvironmentMap(environmentMap);
    }

    m_environmentMap = environmentMap;
}

void Scene::setColorGradingLUT(CubeLUT const* lut)
{
    // TODO: Track current LUT to avoid redundant updates
    if (lut) {
        gpuScene().updateColorGradingLUT(*lut);
    } else {
        static CubeLUT identityLut{};
        gpuScene().updateColorGradingLUT(identityLut);
    }
}

void Scene::generateProbeGridFromBoundingBox()
{
    ark::aabb3 sceneAABB {};

    for (auto const& instance : gpuScene().staticMeshInstances()) {
        if (StaticMesh* staticMesh = gpuScene().staticMeshForHandle(instance->mesh())) {

            ark::aabb3 transformedAABB = staticMesh->boundingBox().transformed(instance->transform().worldMatrix());
            sceneAABB.expandWithPoint(transformedAABB.min);
            sceneAABB.expandWithPoint(transformedAABB.max);

        }
    }

    for (auto const& instance : gpuScene().skeletalMeshInstances()) {
        if (SkeletalMesh* skeletalMesh = gpuScene().skeletalMeshForHandle(instance->mesh())) {

            ark::aabb3 transformedAABB = skeletalMesh->underlyingMesh().boundingBox().transformed(instance->transform().worldMatrix());
            sceneAABB.expandWithPoint(transformedAABB.min);
            sceneAABB.expandWithPoint(transformedAABB.max);

        }
    }

    sceneAABB.max += vec3(1.0f);
    sceneAABB.min -= vec3(1.0f);

    vec3 bounds = sceneAABB.max - sceneAABB.min;

    int indexOfLargest = 0;
    if (bounds.y > bounds.x || bounds.z > bounds.x) {
        if (bounds.y > bounds.z) {
            indexOfLargest = 1;
        } else {
            indexOfLargest = 2;
        }
    }

    vec3 gridCounts = vec3(16, 16, 16);
    gridCounts[indexOfLargest] = 32;

    ProbeGrid generatedProbeGrid {};
    generatedProbeGrid.gridDimensions = Extent3D(static_cast<u32>(gridCounts.x),
                                                 static_cast<u32>(gridCounts.y),
                                                 static_cast<u32>(gridCounts.z));
    generatedProbeGrid.probeSpacing = bounds / gridCounts;
    generatedProbeGrid.offsetToFirst = sceneAABB.min;

    setProbeGrid(generatedProbeGrid);
}

void Scene::clearSelectedObject()
{
    m_selectedObject = nullptr;
}

void Scene::setSelectedObject(IEditorObject& editorObject)
{
    m_selectedObject = &editorObject;
}

void Scene::setSelectedObject(Light& light)
{
    // TODO: Also track type?
    m_selectedObject = &light;
}
void Scene::setSelectedObject(StaticMeshInstance& meshInstance)
{
    // TODO: Also track type?
    m_selectedObject = &meshInstance;
}

EditorGizmo* Scene::raycastScreenPointAgainstEditorGizmos(vec2 screenPoint)
{
    EditorGizmo* closestGizmo = nullptr;

    for (EditorGizmo& gizmo : m_editorGizmos) {
        if (gizmo.isScreenPointInside(screenPoint)) {
            if (closestGizmo == nullptr || gizmo.distanceFromCamera() < closestGizmo->distanceFromCamera()) {
                closestGizmo = &gizmo;
            }
        }
    }

    return closestGizmo;
}

void Scene::drawSettingsGui(bool includeContainingWindow)
{
    if (includeContainingWindow) {
        ImGui::Begin("Scene");
    }

    if (ImGui::TreeNode("Environment")) {
        ImGui::SliderFloat("Ambient (lx)", &m_ambientIlluminance, 0.0f, 1'000.0f, "%.0f");
        // NOTE: Obviously the unit of this is dependent on the values in the texture.. we should probably unify this a bit.
        ImGui::SliderFloat("Environment multiplier", &m_environmentMap.brightnessFactor, 0.0f, 10'000.0f, "%.0f");
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Visualisations")) {
        ImGui::Checkbox("Draw all mesh bounding boxes", &m_shouldDrawAllInstanceBoundingBoxes);
        ImGui::Checkbox("Draw bounding box of the selected mesh instance", &m_shouldDrawSelectedInstanceBoundingBox);
        ImGui::TreePop();
    }


    if (includeContainingWindow) {
        ImGui::End();
    }
}

void Scene::drawInstanceBoundingBox(StaticMeshInstance const& instance)
{
    if (StaticMesh* staticMesh = gpuScene().staticMeshForHandle(instance.mesh())) {
        ark::aabb3 transformedAABB = staticMesh->boundingBox().transformed(instance.transform().worldMatrix());
        DebugDrawer::get().drawBox(transformedAABB.min, transformedAABB.max, vec3(1.0f, 1.0f, 1.0f));
    }
}

void Scene::drawSceneGizmos()
{
    // Reset "persistent" gizmos
    m_editorGizmos.clear();

    static ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
    static ImGuizmo::MODE mode = ImGuizmo::WORLD;

    auto& input = Input::instance();
    if (not input.isButtonDown(Button::Right) && not input.isGuiUsingKeyboard()) {
        if (input.wasKeyPressed(Key::W))
            operation = ImGuizmo::TRANSLATE;
        else if (input.wasKeyPressed(Key::E))
            operation = ImGuizmo::ROTATE;
        else if (input.wasKeyPressed(Key::R))
            operation = ImGuizmo::SCALE;
    }

    if (input.wasKeyPressed(Key::Y) && not input.isGuiUsingKeyboard()) {
        if (mode == ImGuizmo::LOCAL) {
            mode = ImGuizmo::WORLD;
        } else if (mode == ImGuizmo::WORLD) {
            mode = ImGuizmo::LOCAL;
        }
    }

    if (input.wasKeyPressed(Key::G)) {
        m_shouldDrawGizmos = not m_shouldDrawGizmos;
    }

    if (m_shouldDrawGizmos) {
        // Light gizmos
        forEachLight([this](size_t idx, Light& light) {
            Icon const& lightbulbIcon = gpuScene().iconManager().lightbulb();
            IconBillboard iconBillboard = lightbulbIcon.asBillboard(camera(), light.transform().positionInWorld());
            DebugDrawer::get().drawIcon(iconBillboard, light.color());

            EditorGizmo gizmo { iconBillboard, light };
            gizmo.debugName = light.name();
            m_editorGizmos.push_back(gizmo);
        });
    }

    if (m_shouldDrawAllInstanceBoundingBoxes) {
        for (auto const& instance : gpuScene().staticMeshInstances()) {
            drawInstanceBoundingBox(*instance);
        }
    }

    if (selectedObject()) {

        if (m_shouldDrawSelectedInstanceBoundingBox) {
            if (auto* instance = dynamic_cast<StaticMeshInstance*>(selectedObject())) {
                drawInstanceBoundingBox(*instance);
            }
        }

        if (selectedObject()->shouldDrawGui()) {

            constexpr float defaultWindowWidth = 480.0f;
            vec2 windowPosition = vec2(ImGui::GetIO().DisplaySize.x - defaultWindowWidth - 16.0f, 32.0f);
            ImGui::SetNextWindowPos(ImVec2(windowPosition.x, windowPosition.y), ImGuiCond_Appearing);
            ImGui::SetNextWindowSize(ImVec2(defaultWindowWidth, 600.0f), ImGuiCond_Appearing);

            bool open = true;
            constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;

            if (ImGui::Begin("##SelectedObjectWindow", &open, flags)) {
                selectedObject()->drawGui();
            }
            ImGui::End();
        }

        Transform& selectedTransform = selectedObject()->transform();

        ImGuizmo::BeginFrame();
        ImGuizmo::SetRect(0, 0, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);

        mat4 viewMatrix = camera().viewMatrix();
        mat4 projMatrix = camera().projectionMatrix();

        // Silly stuff, since ImGuizmo doesn't seem to like my projection matrix..
        projMatrix.y = -projMatrix.y;

        mat4 matrix = selectedTransform.localMatrix();
        if (ImGuizmo::Manipulate(value_ptr(viewMatrix), value_ptr(projMatrix), operation, mode, value_ptr(matrix))) {
            selectedTransform.setFromMatrix(matrix);
        }
    }
}
