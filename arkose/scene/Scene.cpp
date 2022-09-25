#include "Scene.h"

#include "asset/LevelAsset.h"
#include "asset/StaticMeshAsset.h"
#include "core/Assert.h"
#include "rendering/GpuScene.h"
#include "scene/camera/Camera.h"
#include "physics/PhysicsMesh.h"
#include "physics/PhysicsScene.h"
#include "physics/backend/base/PhysicsBackend.h"
#include "utility/FileIO.h"
#include <imgui.h>
#include <ImGuizmo.h>
#include <nlohmann/json.hpp>
#include <fstream>

Scene::Scene(Backend& backend, PhysicsBackend* physicsBackend, Extent2D initialMainViewportSize)
{
    m_gpuScene = std::make_unique<GpuScene>(*this, backend, initialMainViewportSize);

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

    if (Input::instance().wasKeyReleased(Key::Escape)) {
        setSelectedInstance(nullptr);
    }

    drawSceneGizmos();

    if (hasPhysicsScene()) {
        physicsScene().commitInstancesAwaitingAdd();
    }
}

void Scene::preRender()
{
    SCOPED_PROFILE_ZONE();
    camera().preRender({});
}

void Scene::postRender()
{
    SCOPED_PROFILE_ZONE();

    camera().postRender({});

    for (auto& instance : m_staticMeshInstances) {
        instance->transform.postRender({});
    }
}

void Scene::setupFromDescription(const Description& description)
{
    // NOTE: Must initialize GPU scene before we start registering meshes etc.
    gpuScene().initialize({}, description.maintainRayTracingScene);

    if (FileIO::isFileReadable(description.path)) {
        if (description.path.ends_with("json")) {
            // Old path to be removed!
            loadFromFile(description.path);
        } else {
            if (LevelAsset* levelAsset = LevelAsset::loadFromArklvl(description.path)) {
                addLevel(levelAsset);
            }
        }
    }
}

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
        StaticMeshAsset const* staticMeshAsset = staticMesh->asset();

        sceneObject.name = staticMeshInstance->name;
        sceneObject.transform = translateTransform(staticMeshInstance->transform);

        Arkose::Asset::PathT path;
        path.path = "placeholder-path"; // staticMeshAsset->assetFilePath(); TODO!
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

void Scene::addLevel(LevelAsset* levelAsset)
{
    SCOPED_PROFILE_ZONE();

    auto translateTransform = [](Arkose::Asset::TransformT const& xform) -> Transform {
        Transform result { vec3(xform.position.x(), xform.position.y(), xform.position.z()),
                           quat(vec3(xform.orientation.x(), xform.orientation.y(), xform.orientation.z()), xform.orientation.w()),
                           vec3(xform.scale.x(), xform.scale.y(), xform.scale.z()) };
        return result;
    };

    auto translateVec3 = [](Arkose::Asset::Vec3 v) -> vec3 {
        return vec3(v.x(), v.y(), v.z());
    };

    auto translateQuat = [](Arkose::Asset::Quat q) -> quat {
        return quat(vec3(q.x(), q.y(), q.z()), q.w());
    };

    for (std::unique_ptr<SceneObjectAsset> const& sceneObjectAsset : levelAsset->objects) {

        // TODO: Handle non-path indirection
        ARKOSE_ASSERT(sceneObjectAsset->mesh_asset.type == Arkose::Asset::MeshIndirection::path);
        std::string const& staticMeshAssetPath = sceneObjectAsset->mesh_asset.Aspath()->path;
        StaticMeshAsset* staticMeshAsset = StaticMeshAsset::loadFromArkmsh(staticMeshAssetPath);

        Transform transform = translateTransform(*sceneObjectAsset->transform);

        StaticMeshInstance& instance = addMesh(staticMeshAsset, transform);
        instance.name = sceneObjectAsset->name;

    }

    for (LightAsset const& lightAsset : levelAsset->lights) {
        if (DirectionalLightAsset const* dirLightAsset = lightAsset.AsDirectionalLight()) {

            Transform transform = translateTransform(*dirLightAsset->transform);
            vec3 direction = ark::rotateVector(transform.orientationInWorld(), ark::globalForward);

            auto dirLight = std::make_unique<DirectionalLight>(translateVec3(dirLightAsset->color), dirLightAsset->illuminance, direction);
            
            dirLight->setName(dirLightAsset->name);
            dirLight->shadowMapWorldExtent = dirLightAsset->world_extent;

            addLight(std::move(dirLight));

        } else if (SpotLightAsset const* spotLightAsset = lightAsset.AsSpotLight()) {
        
            Transform transform = translateTransform(*spotLightAsset->transform);
            vec3 direction = ark::rotateVector(transform.orientationInWorld(), ark::globalForward);

            // TODO: Handle non-path indirection
            ARKOSE_ASSERT(spotLightAsset->ies_profile.type == Arkose::Asset::ImageIndirection::path);
            std::string const& iesProfileFilePath = spotLightAsset->ies_profile.Aspath()->path;

            auto spotLight = std::make_unique<SpotLight>(translateVec3(spotLightAsset->color), spotLightAsset->luminous_intensity,
                                                         iesProfileFilePath, transform.positionInWorld(), direction);

            spotLight->setName(spotLightAsset->name);

            addLight(std::move(spotLight));

        }
    }

    for (std::unique_ptr<CameraAsset> const& cameraAsset : levelAsset->cameras) {

        Camera& camera = addCamera(cameraAsset->name, false);
        camera.setPosition(translateVec3(cameraAsset->transform->position));
        camera.setOrientation(translateQuat(cameraAsset->transform->orientation));

        if (auto* manualExposure = cameraAsset->exposure.AsManualExposure()) {
            camera.setManualExposureParameters(manualExposure->f_number, manualExposure->shutter_speed, manualExposure->iso);
        } else if (auto* autoExposure = cameraAsset->exposure.AsAutoExposure()) {
            ASSERT_NOT_REACHED();
        }

    }

    if (EnvironmentMapAsset const* environmentMapAsset = levelAsset->environment_map.get()) {

        // TODO: Handle non-path indirection
        ARKOSE_ASSERT(environmentMapAsset->image.type == Arkose::Asset::ImageIndirection::path);
        std::string const& imageAssetPath = environmentMapAsset->image.Aspath()->path;

        setEnvironmentMap({ .assetPath = imageAssetPath,
                            .brightnessFactor = environmentMapAsset->brightness_factor });
    }

    if (ProbeGridAsset const* probeGrid = levelAsset->probe_grid.get()) {

        setProbeGrid({ .gridDimensions = Extent3D(static_cast<uint32_t>(probeGrid->dimensions.x()),
                                                  static_cast<uint32_t>(probeGrid->dimensions.y()),
                                                  static_cast<uint32_t>(probeGrid->dimensions.z())),
                       .probeSpacing = translateVec3(probeGrid->spacing),
                       .offsetToFirst = translateVec3(probeGrid->offset) });

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

std::vector<StaticMeshInstance*> Scene::loadMeshes(const std::string& filePath)
{
    // For now we only load glTF
    ARKOSE_ASSERT(filePath.ends_with(".gltf") || filePath.ends_with(".glb"));
    GltfLoader::LoadResult result = m_gltfLoader.load(filePath);

    // Register materials & create translation table from local to global material handles

    std::unordered_map<MaterialHandle, MaterialHandle> materialHandleMap;

    for (size_t localMaterialIdx = 0; localMaterialIdx < result.materials.size(); ++localMaterialIdx) {

        MaterialHandle localMaterialHandle = MaterialHandle(static_cast<MaterialHandle::IndexType>(localMaterialIdx));
        MaterialHandle materialHandle = gpuScene().registerMaterial(*result.materials[localMaterialIdx]);

        materialHandleMap[localMaterialHandle] = materialHandle;
    }

    // Translate material handles for meshes

    for (auto& staticMesh : result.staticMeshes) {
        for (StaticMeshLOD& lod : staticMesh->LODs()) {
            for (StaticMeshSegment& segment : lod.meshSegments) {

                auto entry = materialHandleMap.find(segment.material);
                ARKOSE_ASSERT(entry != materialHandleMap.end());

                segment.material = entry->second;
            }
        }
    }

    // Create physics representations for loaded meshes
    // NOTE: When loading in akblob meshes they should already have physics setup

    if (hasPhysicsScene()) {

        for (auto& staticMesh : result.staticMeshes) {
            for (StaticMeshLOD& lod : staticMesh->LODs()) {

                // From a phyiscs point of view, all segments are together one shape, but they can have multiple physics materials.

                std::vector<PhysicsMesh> physicsMeshes {};
                physicsMeshes.reserve(lod.meshSegments.size());

                for (StaticMeshSegment& segment : lod.meshSegments) {

                    PhysicsMesh physicsMesh;
                    physicsMesh.positions = segment.positions;
                    physicsMesh.indices = segment.indices;
                    physicsMesh.material = PhysicsMaterial();

                    physicsMeshes.push_back(physicsMesh);
                }

                PhysicsShapeHandle physicsShape = physicsScene().backend().createPhysicsShapeForTriangleMeshes(physicsMeshes);
                lod.physicsShape = physicsShape;

            }
        }

    }

    // Add meshes & create instances

    std::vector<StaticMeshInstance*> instances {};

    for (auto& staticMesh : result.staticMeshes) {
        StaticMeshHandle staticMeshHandle = gpuScene().registerStaticMesh(staticMesh);
        StaticMeshInstance& instance = createStaticMeshInstance(staticMeshHandle, Transform());
        instances.push_back(&instance);
    }

    return instances;
}

StaticMeshInstance& Scene::addMesh(StaticMeshAsset* staticMesh, Transform transform)
{
    ARKOSE_ASSERT(staticMesh != nullptr);

    StaticMeshHandle staticMeshHandle = gpuScene().registerStaticMesh(staticMesh);
    StaticMeshInstance& instance = createStaticMeshInstance(staticMeshHandle, transform);

    if (hasPhysicsScene()) {
        // TODO!
    }

    return instance;
}

void Scene::unloadAllMeshes()
{
    for (auto& instance : m_staticMeshInstances) {
        gpuScene().unregisterStaticMesh(instance->mesh);
    }

    m_staticMeshInstances.clear();
}

StaticMeshInstance& Scene::createStaticMeshInstance(StaticMeshHandle staticMeshHandle, Transform transform)
{
    m_staticMeshInstances.push_back(std::make_unique<StaticMeshInstance>(staticMeshHandle, transform));
    StaticMeshInstance& instance = *m_staticMeshInstances.back();

    if (hasPhysicsScene()) {
        // TODO: What about the other LODs?
        StaticMesh* staticMesh = gpuScene().staticMeshForHandle(staticMeshHandle);
        if (staticMesh->numLODs() > 0 && staticMesh->lodAtIndex(0).physicsShape.valid()) {
            PhysicsShapeHandle physicsShape = staticMesh->lodAtIndex(0).physicsShape;
            PhysicsInstanceHandle physicsInstanceHandle = physicsScene().createInstance(physicsShape, MotionType::Static, Transform());
            instance.physicsInstance = physicsInstanceHandle;
        }
    }

    return instance;
}

DirectionalLight& Scene::addLight(std::unique_ptr<DirectionalLight> light)
{
    ARKOSE_ASSERT(light);
    m_directionalLights.push_back(std::move(light));
    DirectionalLight& addedLight = *m_directionalLights.back();
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

void Scene::generateProbeGridFromBoundingBox()
{
    NOT_YET_IMPLEMENTED();

    /*

    constexpr int maxGridSideSize = 16;
    constexpr float boxPadding = 0.0f;

    ark::aabb3 sceneBox {};
    scene().forEachMesh([&](size_t, Mesh& mesh) {
        // TODO: Transform the bounding box first, obviously..
        // But we aren't using this path right now so not going
        // to spend time on it right now.
        ark::aabb3 meshBox = mesh.boundingBox();
        sceneBox.expandWithPoint(meshBox.min);
        sceneBox.expandWithPoint(meshBox.max);
    });
    sceneBox.min -= vec3(boxPadding);
    sceneBox.max += vec3(boxPadding);

    vec3 dims = sceneBox.max - sceneBox.min;
    int counts[3] = { maxGridSideSize, maxGridSideSize, maxGridSideSize };
    int indexOfSmallest = 0;
    if (dims.y < dims.x || dims.z < dims.x) {
        if (dims.y < dims.z) {
            indexOfSmallest = 1;
        } else {
            indexOfSmallest = 2;
        }
    }
    counts[indexOfSmallest] /= 2;

    vec3 spacing = dims / vec3((float)counts[0], (float)counts[1], (float)counts[2]);

    ProbeGrid grid;
    grid.offsetToFirst = sceneBox.min;
    grid.gridDimensions = Extent3D(counts[0], counts[1], counts[2]);
    grid.probeSpacing = spacing;
    setProbeGrid(grid);

    */
}

void Scene::loadFromFile(const std::string& path)
{
    SCOPED_PROFILE_ZONE();

    using json = nlohmann::json;

    json jsonScene;
    std::ifstream fileStream(path);
    fileStream >> jsonScene;

    auto readVec3 = [&](const json& val) -> vec3 {
        std::vector<float> values = val;
        ARKOSE_ASSERT(values.size() == 3);
        return { values[0], values[1], values[2] };
    };

    auto readExtent3D = [&](const json& val) -> Extent3D {
        std::vector<uint32_t> values = val;
        ARKOSE_ASSERT(values.size() == 3);
        return Extent3D(values[0], values[1], values[2]);
    };

    auto optionallyParseLightName = [](const json& jsonLight, Light& light) {
        if (jsonLight.find("name") != jsonLight.end())
            light.setName(jsonLight.at("name"));
    };

    auto jsonEnv = jsonScene.at("environment");
    EnvironmentMap envMap;
    envMap.assetPath = jsonEnv.at("texture");
    envMap.brightnessFactor = jsonEnv.at("illuminance");
    setEnvironmentMap(envMap);

    for (auto& jsonModel : jsonScene.at("models")) {
        std::string modelGltf = jsonModel.at("gltf");

        auto transform = jsonModel.at("transform");
        auto jsonRotation = transform.at("rotation");

        quat orientation;
        std::string rotType = jsonRotation.at("type");
        if (rotType == "axis-angle") {
            vec3 axis = readVec3(jsonRotation.at("axis"));
            float angle = jsonRotation.at("angle");
            orientation = ark::axisAngle(axis, angle);
        } else {
            ASSERT_NOT_REACHED();
        }

        vec3 translation = readVec3(transform.at("translation"));
        vec3 scale = readVec3(transform.at("scale"));

        Transform xform { translation, orientation, scale };

        std::string modelName = jsonModel.at("name");

        std::vector<StaticMeshInstance*> loadedInstances = loadMeshes(modelGltf);
        for (StaticMeshInstance* instance : loadedInstances) {

            // NOTE: We might want to add a suffix number for each separate mesh?
            instance->name = modelName;

            instance->transform = xform;
        }
    }

    for (auto& jsonLight : jsonScene.at("lights")) {

        auto type = jsonLight.at("type");
        if (type == "directional") {

            vec3 color = readVec3(jsonLight.at("color"));
            float illuminance = jsonLight.at("illuminance");
            vec3 direction = readVec3(jsonLight.at("direction"));

            auto light = std::make_unique<DirectionalLight>(color, illuminance, direction);

            optionallyParseLightName(jsonLight, *light);

            light->shadowMapWorldOrigin = { 0, 0, 0 };
            light->shadowMapWorldExtent = jsonLight.at("worldExtent");

            addLight(std::move(light));

        } else if (type == "spot") {

            vec3 color = readVec3(jsonLight.at("color"));
            float luminousIntensity = jsonLight.at("luminousIntensity");
            vec3 position = readVec3(jsonLight.at("position"));
            vec3 direction = readVec3(jsonLight.at("direction"));
            std::string iesPath = jsonLight.at("ies");

            auto light = std::make_unique<SpotLight>(color, luminousIntensity, iesPath, position, direction);

            optionallyParseLightName(jsonLight, *light);

            addLight(std::move(light));

        } else if (type == "ambient") {

            float illuminance = jsonLight.at("illuminance");
            setAmbientIlluminance(illuminance);

        } else {
            ASSERT_NOT_REACHED();
        }
    }

    if (jsonScene.find("probe-grid") != jsonScene.end()) {
        auto jsonProbeGrid = jsonScene.at("probe-grid");
        setProbeGrid({ .gridDimensions = readExtent3D(jsonProbeGrid.at("dimensions")),
                       .probeSpacing = readVec3(jsonProbeGrid.at("spacing")),
                       .offsetToFirst = readVec3(jsonProbeGrid.at("offsetToFirst")) });
    }

    for (auto& jsonCamera : jsonScene.at("cameras")) {

        auto camera = std::make_unique<Camera>();

        vec3 position = readVec3(jsonCamera.at("position"));
        vec3 direction = normalize(readVec3(jsonCamera.at("direction")));
        camera->lookAt(position, position + direction, ark::globalUp);

        if (jsonCamera.find("exposure") != jsonCamera.end()) {
            if (jsonCamera.at("exposure") == "manual") {
                float iso = jsonCamera.at("ISO");
                float aperture = jsonCamera.at("aperture");
                float shutterSpeed = 1.0f / jsonCamera.at("shutter");
                camera->setExposureMode(Camera::ExposureMode::Manual);
                camera->setManualExposureParameters(aperture, shutterSpeed, iso);
            } else if (jsonCamera.at("exposure") == "auto") {
                float exposureCompensation = jsonCamera.at("EC");
                float adaptionRate = jsonCamera.at("adaptionRate");
                camera->setExposureMode(Camera::ExposureMode::Auto);
                camera->setExposureCompensation(exposureCompensation);
                camera->setAutoExposureAdaptionRate(adaptionRate);
            }
        }

        std::string name = jsonCamera.at("name");
        m_allCameras[name] = std::move(camera);
    }

    std::string mainCamera = jsonScene.at("camera");
    auto entry = m_allCameras.find(mainCamera);
    if (entry != m_allCameras.end()) {
        m_currentMainCamera = m_allCameras[mainCamera].get();
    }
}

void Scene::drawSettingsGui(bool includeContainingWindow)
{
    if (includeContainingWindow) {
        ImGui::Begin("Scene");
    }

    if (ImGui::TreeNode("Film grain")) {
        // TODO: I would love to estimate gain grain from ISO and scene light amount, but that's for later..
        ImGui::SliderFloat("Fixed grain gain", &m_fixedFilmGrainGain, 0.0f, 0.25f);
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Environment")) {
        ImGui::SliderFloat("Ambient (lx)", &m_ambientIlluminance, 0.0f, 1'000.0f, "%.0f");
        // NOTE: Obviously the unit of this is dependent on the values in the texture.. we should probably unify this a bit.
        ImGui::SliderFloat("Environment multiplier", &m_environmentMap.brightnessFactor, 0.0f, 10'000.0f, "%.0f");
        ImGui::TreePop();
    }

    ImGui::Separator();

    {
        static Light* selectedLight = nullptr;
        if (ImGui::BeginCombo("Inspected light", selectedLight ? selectedLight->name().c_str() : "Select a light")) {
            forEachLight([&](size_t lightIndex, Light& light) {
                bool selected = &light == selectedLight;
                if (ImGui::Selectable(light.name().c_str(), &selected))
                    selectedLight = &light;
                if (selected)
                    ImGui::SetItemDefaultFocus();
            });
            ImGui::EndCombo();
        }

        if (selectedLight != nullptr) {

            ImGui::ColorEdit3("Color", value_ptr(selectedLight->color));

            switch (selectedLight->type()) {
            case Light::Type::DirectionalLight:
                ImGui::SliderFloat("Illuminance (lx)", &static_cast<DirectionalLight*>(selectedLight)->illuminance, 1.0f, 150000.0f);
                break;
            case Light::Type::SpotLight:
                ImGui::SliderFloat("Luminous intensity (cd)", &static_cast<SpotLight*>(selectedLight)->luminousIntensity, 1.0f, 1000.0f);
                break;
            case Light::Type::PointLight:
                break;
            default:
                ASSERT_NOT_REACHED();
            }

            ImGui::SliderFloat("Constant bias", &selectedLight->customConstantBias, 0.0f, 20.0f);
            ImGui::SliderFloat("Slope bias", &selectedLight->customSlopeBias, 0.0f, 10.0f);
        }
    }

    if (includeContainingWindow) {
        ImGui::End();
    }
}

void Scene::drawSceneGizmos()
{
    static ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;

    auto& input = Input::instance();
    if (input.wasKeyPressed(Key::T))
        operation = ImGuizmo::TRANSLATE;
    else if (input.wasKeyPressed(Key::R))
        operation = ImGuizmo::ROTATE;
    else if (input.wasKeyPressed(Key::Y))
        operation = ImGuizmo::SCALE;

    if (selectedInstance()) {

        ImGuizmo::BeginFrame();
        ImGuizmo::SetRect(0, 0, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);

        // FIXME: Support world transforms! Well, we don't really have hierarchies right now, so it doesn't really matter.
        //  What we do have is meshes with their own transform under a model, and we are modifying the model's transform here.
        //  Maybe in the future we want to be able to modify meshes too?
        ImGuizmo::MODE mode = ImGuizmo::LOCAL;

        mat4 viewMatrix = camera().viewMatrix();
        mat4 projMatrix = camera().projectionMatrix();

        // Silly stuff, since ImGuizmo doesn't seem to like my projection matrix..
        projMatrix.y = -projMatrix.y;

        mat4 matrix = selectedInstance()->transform.localMatrix();
        ImGuizmo::Manipulate(value_ptr(viewMatrix), value_ptr(projMatrix), operation, mode, value_ptr(matrix));
        selectedInstance()->transform.setFromMatrix(matrix);
    }
}
