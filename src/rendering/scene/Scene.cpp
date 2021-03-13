#include "Scene.h"

#include "rendering/Registry.h"
#include "rendering/scene/models/GltfModel.h"
#include "utility/FileIO.h"
#include "utility/Logging.h"
#include <fstream>
#include <imgui.h>
#include <moos/aabb.h>
#include <moos/transform.h>
#include <nlohmann/json.hpp>

Scene::Scene(Registry& registry)
    : m_registry(registry)
{
}

void Scene::loadFromFile(const std::string& path)
{
    using json = nlohmann::json;

    if (!FileIO::isFileReadable(path))
        LogErrorAndExit("Could not read scene file '%s', exiting\n", path.c_str());
    m_filePath = path;

    json jsonScene;
    std::ifstream fileStream(path);
    fileStream >> jsonScene;

    auto readVec3 = [&](const json& val) -> vec3 {
        std::vector<float> values = val;
        ASSERT(values.size() == 3);
        return { values[0], values[1], values[2] };
    };

    auto readExtent3D = [&](const json& val) -> Extent3D {
        std::vector<uint32_t> values = val;
        ASSERT(values.size() == 3);
        return Extent3D(values[0], values[1], values[2]);
    };

    auto jsonEnv = jsonScene.at("environment");
    m_environmentMap = jsonEnv.at("texture");
    m_environmentMultiplier = jsonEnv.at("illuminance");

    for (auto& jsonModel : jsonScene.at("models")) {
        std::string modelGltf = jsonModel.at("gltf");

        auto model = GltfModel::load(modelGltf);
        if (!model)
            continue;

        std::string name = jsonModel.at("name");
        model->setName(name);

        auto transform = jsonModel.at("transform");
        auto jsonRotation = transform.at("rotation");

        mat4 rotationMatrix;
        std::string rotType = jsonRotation.at("type");
        if (rotType == "axis-angle") {
            vec3 axis = readVec3(jsonRotation.at("axis"));
            float angle = jsonRotation.at("angle");
            rotationMatrix = moos::quatToMatrix(moos::axisAngle(axis, angle));
        } else {
            ASSERT_NOT_REACHED();
        }

        mat4 localMatrix = moos::translate(readVec3(transform.at("translation")))
            * rotationMatrix * moos::scale(readVec3(transform.at("scale")));
        model->transform().setLocalMatrix(localMatrix);

        addModel(std::move(model));
    }

    for (auto& jsonLight : jsonScene.at("lights")) {

        auto type = jsonLight.at("type");
        if (type == "directional") {

            vec3 color = readVec3(jsonLight.at("color"));
            float illuminance = jsonLight.at("illuminance");
            vec3 direction = readVec3(jsonLight.at("direction"));

            auto light = std::make_unique<DirectionalLight>(color, illuminance, direction);

            light->shadowMapWorldOrigin = { 0, 0, 0 };
            light->shadowMapWorldExtent = jsonLight.at("worldExtent");

            int mapSize[2];
            jsonLight.at("shadowMapSize").get_to(mapSize);
            light->setShadowMapSize({ mapSize[0], mapSize[1] });

            addLight(std::move(light));

        } else if (type == "ambient") {

            float illuminance = jsonLight.at("illuminance");
            m_ambientIlluminance = illuminance;

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

        FpsCamera camera;
        vec3 origin = readVec3(jsonCamera.at("origin"));
        vec3 target = readVec3(jsonCamera.at("target"));
        camera.lookAt(origin, target, moos::globalUp);

        std::string name = jsonCamera.at("name");
        m_allCameras[name] = camera;
    }

    std::string mainCamera = jsonScene.at("camera");
    auto entry = m_allCameras.find(mainCamera);
    if (entry != m_allCameras.end()) {
        m_currentMainCamera = m_allCameras[mainCamera];
    }
}

void Scene::manageResources()
{
    // TODO: Move stuff from SceneNode to here!
}

Model& Scene::addModel(std::unique_ptr<Model> model)
{
    ASSERT(model);
    model->setScene({}, this);
    m_models.push_back(std::move(model));
    return *m_models.back().get();
}

DirectionalLight& Scene::addLight(std::unique_ptr<DirectionalLight> light)
{
    ASSERT(light);
    light->setScene({}, this);
    m_directionalLights.push_back(std::move(light));
    return *m_directionalLights.back().get();
}

SpotLight& Scene::addLight(std::unique_ptr<SpotLight> light)
{
    ASSERT(light);
    light->setScene({}, this);
    m_spotLights.push_back(std::move(light));
    return *m_spotLights.back().get();
}

size_t Scene::meshCount() const
{
    size_t count = 0u;
    for (auto& model : m_models) {
        count += model->meshCount();
    }
    return count;
}

void Scene::forEachModel(std::function<void(size_t, const Model&)> callback) const
{
    for (size_t i = 0; i < m_models.size(); ++i) {
        const Model& model = *m_models[i];
        callback(i, model);
    }
}

void Scene::forEachModel(std::function<void(size_t, Model&)> callback)
{
    for (size_t i = 0; i < m_models.size(); ++i) {
        Model& model = *m_models[i];
        callback(i, model);
    }
}

int Scene::forEachMesh(std::function<void(size_t, const Mesh&)> callback) const
{
    size_t nextIndex = 0;
    for (auto& model : m_models) {
        model->forEachMesh([&](const Mesh& mesh) {
            callback(nextIndex++, mesh);
        });
    }
    return nextIndex;
}

int Scene::forEachMesh(std::function<void(size_t, Mesh&)> callback)
{
    size_t nextIndex = 0;
    for (auto& model : m_models) {
        model->forEachMesh([&](Mesh& mesh) {
            callback(nextIndex++, mesh);
        });
    }
    return nextIndex;
}

int Scene::forEachLight(std::function<void(size_t, const Light&)> callback) const
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

int Scene::forEachLight(std::function<void(size_t, Light&)> callback)
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

void Scene::generateProbeGridFromBoundingBox()
{
    NOT_YET_IMPLEMENTED();

    constexpr int maxGridSideSize = 16;
    constexpr float boxPadding = 0.0f;

    moos::aabb3 sceneBox {};
    forEachMesh([&](size_t, Mesh& mesh) {
        // TODO: Transform the bounding box first, obviously..
        // But we aren't using this path right now so not going
        // to spend time on it right now.
        moos::aabb3 meshBox = mesh.boundingBox();
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

    vec3 spacing = dims / vec3(counts[0], counts[1], counts[2]);

    ProbeGrid grid;
    grid.offsetToFirst = sceneBox.min;
    grid.gridDimensions = Extent3D(counts[0], counts[1], counts[2]);
    grid.probeSpacing = spacing;
    setProbeGrid(grid);
}
