#include "Scene.h"

#include "rendering/Registry.h"
#include "rendering/scene/models/GltfModel.h"
#include "utility/FileIO.h"
#include "utility/Logging.h"
#include <fstream>
#include <imgui.h>
#include <mooslib/transform.h>
#include <nlohmann/json.hpp>

mat4 SunLight::lightProjection() const
{
    mat4 lightOrientation = moos::lookAt({ 0, 0, 0 }, normalize(direction)); // (note: currently just centered on the origin)
    mat4 lightProjection = moos::orthographicProjectionToVulkanClipSpace(worldExtent, -worldExtent, worldExtent);
    return lightProjection * lightOrientation;
}

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

    auto jsonEnv = jsonScene.at("environment");
    m_environmentMap = jsonEnv.at("texture");
    m_environmentMultiplier = jsonEnv.at("multiplier");

    for (auto& jsonModel : jsonScene.at("models")) {
        std::string modelGltf = jsonModel.at("gltf");

        auto model = GltfModel::load(modelGltf);
        if (!model) {
            continue;
        }

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

        std::vector<float> translation = transform.at("translation");
        std::vector<float> scale = transform.at("scale");

        mat4 rotationMatrix;
        auto jsonRotation = transform.at("rotation");
        std::string rotType = jsonRotation.at("type");
        if (rotType == "axis-angle") {
            float angle = jsonRotation.at("angle");
            std::vector<float> axis = jsonRotation.at("axis");
            rotationMatrix = moos::quatToMatrix(moos::axisAngle({ axis[0], axis[1], axis[2] }, angle));
        } else {
            ASSERT_NOT_REACHED();
        }

        mat4 localMatrix = moos::translate(vec3(translation[0], translation[1], translation[2]))
            * rotationMatrix * moos::scale(vec3(scale[0], scale[1], scale[2]));
        model->transform().setLocalMatrix(localMatrix);

        addModel(std::move(model));
    }

    for (auto& jsonLight : jsonScene.at("lights")) {
        ASSERT(jsonLight.at("type") == "directional");

        SunLight sun;

        float color[3];
        jsonLight.at("color").get_to(color);
        sun.color = { color[0], color[1], color[2] };

        sun.intensity = jsonLight.at("intensity");

        float dir[3];
        jsonLight.at("direction").get_to(dir);
        sun.direction = normalize(vec3(dir[0], dir[1], dir[2]));

        sun.worldExtent = jsonLight.at("worldExtent");

        int mapSize[2];
        jsonLight.at("shadowMapSize").get_to(mapSize);
        sun.shadowMapSize = { mapSize[0], mapSize[1] };

        // TODO!
        m_sunLight = sun;
    }

    for (auto& jsonCamera : jsonScene.at("cameras")) {

        FpsCamera camera;

        std::string name = jsonCamera.at("name");

        float origin[3];
        jsonCamera.at("origin").get_to(origin);

        float target[3];
        jsonCamera.at("target").get_to(target);

        camera.lookAt({ origin[0], origin[1], origin[2] }, { target[0], target[1], target[2] }, moos::globalUp);
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
    MOOSLIB_ASSERT(model);
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
