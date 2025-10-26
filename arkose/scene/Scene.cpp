#include "Scene.h"

#include "asset/LevelAsset.h"
#include "asset/MeshAsset.h"
#include "asset/SetAsset.h"
#include "asset/external/CubeLUT.h"
#include "core/Assert.h"
#include "system/Input.h"
#include "rendering/GpuScene.h"
#include "rendering/RenderPipeline.h"
#include "scene/camera/Camera.h"
#include "scene/editor/EditorScene.h"
#include "physics/PhysicsMesh.h"
#include "physics/PhysicsScene.h"
#include "physics/backend/base/PhysicsBackend.h"
#include "utility/FileIO.h"
#include <imgui.h>
#include <fstream>

Scene::Scene(Backend& backend, PhysicsBackend* physicsBackend)
{
    m_rootNode = m_sceneNodes.add(SceneNode(*this, Transform(), "Root"));
    m_sceneNodes.markPersistent(m_rootNode);

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

    m_sceneNodes.processDeferredDeletes(0, 0, [this](SceneNodeHandle sceneNodeHandle, SceneNode& sceneNode) {
        for (SceneNodeHandle child : sceneNode.children()) {
            removeNode(child);
        }
        sceneNode.m_children.clear();
    });

    for (auto& skeletalMeshInstance : gpuScene().skeletalMeshInstances()) {
        if (skeletalMeshInstance->hasSkeleton()) {
            skeletalMeshInstance->skeleton().applyJointTransformations();
        }
    }

    if (hasEditorScene()) { 
        editorScene().update(elapsedTime, deltaTime);
    }

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
        if (FileIO::fileReadable(description.path)) {
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

    if (description.createEditorScene) {
        m_editorScene = std::make_unique<EditorScene>(*this);
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

        if (!sceneObjectAsset.set.empty()) {

            SetAsset* setAsset = SetAsset::load(sceneObjectAsset.set);
            SceneNodeHandle setHandle = addSet(setAsset);
            (void)setHandle;

        } else {

            // TODO: Handle non-path indirection
            std::string const& meshAssetPath = std::string(sceneObjectAsset.pathToMesh());
            MeshAsset* meshAsset = MeshAsset::load(meshAssetPath);

            StaticMeshInstance& instance = addMesh(meshAsset, sceneObjectAsset.transform);
            instance.name = sceneObjectAsset.name;
        }

    }

    for (LightAsset const& lightAsset : levelAsset->lights) {
        if (lightAsset.type == "DirectionalLight") {
            addLight(std::make_unique<DirectionalLight>(lightAsset));
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

SceneNodeHandle Scene::addNode(Transform const& transform, std::string_view name, SceneNodeHandle parent)
{
    SceneNodeHandle nodeHandle = m_sceneNodes.add(SceneNode(*this, transform, name));

    SceneNode* newNode = node(nodeHandle);
    newNode->setHandle(nodeHandle, {});
    newNode->setParent(parent);

    return nodeHandle;
}

void Scene::removeNode(SceneNodeHandle nodeHandle)
{
    m_sceneNodes.removeReference(nodeHandle, 0);
}

SceneNodeHandle Scene::addSet(SetAsset* setAsset)
{
    setAsset->rootNode.name = setAsset->name;
    return addNodeRecursive(setAsset, &setAsset->rootNode, m_rootNode);
}

SceneNodeHandle Scene::addSet(SetAsset* setAsset, SceneNodeHandle parent)
{
    return addNodeRecursive(setAsset, &setAsset->rootNode, parent);
}

SceneNodeHandle Scene::addNodeRecursive(SetAsset* setAsset, NodeAsset* nodeAsset, SceneNodeHandle parent)
{
    SceneNodeHandle currentNodeHandle = addNode(nodeAsset->transform, nodeAsset->name, parent);

    if (nodeAsset->meshIndex != NodeAsset::InvalidIndex) {
        MeshAsset* meshAsset = MeshAsset::load(setAsset->meshAssets[nodeAsset->meshIndex]);

        // TODO: In theory no need for a transform on the instance iself anymore now, as the node has all the transform hierarchy.
        // But for now, let's just make the mesh's transform a direct child of the node's transform, with no local transforms.
        Transform* attachedNodeTransform = &node(currentNodeHandle)->transform();
        StaticMeshInstance& instance = addMesh(meshAsset, Transform(attachedNodeTransform));

        // TODO: This should just be the node name now.. But for now, let's duplicate it here.
        instance.name = nodeAsset->name;
    }

    for (std::unique_ptr<NodeAsset> const& child : nodeAsset->children) {
        addNodeRecursive(setAsset, child.get(), currentNodeHandle);
    }

    return currentNodeHandle;
}

void Scene::clearScene()
{
    clearAllMeshInstances();

    SceneNode* rootNode = node(m_rootNode);
    for (SceneNodeHandle child : rootNode->children()) {
        removeNode(child);
    }
    rootNode->m_children.clear();

    if (hasEditorScene()) {
        editorScene().clearSelectedObject();
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
    ARKOSE_ASSERT(addedLight.transform().localOrientation().isNormalized());
    gpuScene().registerLight(addedLight);
    return addedLight;
}

SpotLight& Scene::addLight(std::unique_ptr<SpotLight> light)
{
    ARKOSE_ASSERT(light);
    m_spotLights.push_back(std::move(light));
    SpotLight& addedLight = *m_spotLights.back();
    ARKOSE_ASSERT(addedLight.transform().localOrientation().isNormalized());
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

    if (hasEditorScene()) { 
        editorScene().drawGui();
    }

    if (includeContainingWindow) {
        ImGui::End();
    }
}
