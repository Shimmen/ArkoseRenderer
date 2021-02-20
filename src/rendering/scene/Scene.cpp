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
    m_loadedPath = path;

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

        if (jsonModel.find("proxy") != jsonModel.end()) {
            std::string proxyPath = jsonModel.at("proxy");
            auto proxy = loadProxy(proxyPath);
            if (proxy) {
                model->setProxy(std::move(proxy));
            }
        }

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

            DirectionalLight light { color, illuminance, direction };

            light.shadowMapWorldOrigin = { 0, 0, 0 };
            light.shadowMapWorldExtent = jsonLight.at("worldExtent");

            int mapSize[2];
            jsonLight.at("shadowMapSize").get_to(mapSize);
            light.setShadowMapSize({ mapSize[0], mapSize[1] });

            light.setScene({}, this);

            // TODO!
            m_directionalLights.push_back(light);

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

    loadAdditionalCameras();

    std::string mainCamera = jsonScene.at("camera");
    auto entry = m_allCameras.find(mainCamera);
    if (entry != m_allCameras.end()) {
        m_currentMainCamera = m_allCameras[mainCamera];
    }
}

Scene::~Scene()
{
    using json = nlohmann::json;
    json savedCameras;

    if (FileIO::isFileReadable(savedCamerasFile)) {
        std::ifstream fileStream(savedCamerasFile);
        fileStream >> savedCameras;
    }

    json jsonCameras = json::object();

    for (const auto& [name, camera] : m_allCameras) {
        if (name == "main") {
            continue;
        }

        float posData[3];
        posData[0] = camera.position().x;
        posData[1] = camera.position().y;
        posData[2] = camera.position().z;

        float rotData[4];
        rotData[0] = camera.orientation().w;
        rotData[1] = camera.orientation().vec.x;
        rotData[2] = camera.orientation().vec.y;
        rotData[3] = camera.orientation().vec.z;

        jsonCameras[name] = {
            { "position", posData },
            { "orientation", rotData }
        };
    }

    savedCameras[m_loadedPath] = jsonCameras;

    std::ofstream fileStream(savedCamerasFile);
    fileStream << savedCameras;
}

Model& Scene::addModel(std::unique_ptr<Model> model)
{
    ASSERT(model);
    model->setScene({}, this);
    m_models.push_back(std::move(model));
    return *m_models.back().get();
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

void Scene::cameraGui()
{
    for (const auto& [name, camera] : m_allCameras) {
        if (ImGui::Button(name.c_str())) {
            m_currentMainCamera = camera;
        }
    }

    ImGui::Separator();
    static char nameBuffer[63];
    ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer), ImGuiInputTextFlags_AutoSelectAll);

    bool hasName = std::strlen(nameBuffer) > 0;
    if (hasName && ImGui::Button("Save current")) {
        m_allCameras[nameBuffer] = m_currentMainCamera;
    }
}

void Scene::loadAdditionalCameras()
{
    using json = nlohmann::json;

    json savedCameras;
    if (FileIO::isFileReadable(savedCamerasFile)) {
        std::ifstream fileStream(savedCamerasFile);
        fileStream >> savedCameras;
    }

    auto savedCamerasForFile = savedCameras[m_loadedPath];

    for (auto& [name, jsonCamera] : savedCamerasForFile.items()) {
        FpsCamera camera {};

        float posData[3];
        jsonCamera.at("position").get_to(posData);
        camera.setPosition({ posData[0], posData[1], posData[2] });

        float rotData[4];
        jsonCamera.at("orientation").get_to(rotData);
        camera.setOrientation({ { rotData[0], rotData[1], rotData[2] }, rotData[3] });

        m_allCameras[name] = camera;
    }
}

std::unique_ptr<Model> Scene::loadProxy(const std::string& path)
{
    std::string extension = path.substr(path.length() - 4);
    if (extension == "json") {
        ASSERT(false);
    }

    // Else, assume it's a gltf and simply fail if it isn't..
    return GltfModel::load(path);
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
