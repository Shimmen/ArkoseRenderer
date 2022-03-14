#include "Scene.h"

#include "rendering/camera/Camera.h"
#include "rendering/camera/FpsCamera.h"
#include "rendering/scene/models/GltfModel.h"
#include "utility/FileIO.h"
#include <nlohmann/json.hpp>
#include <fstream>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

void Scene::loadFromFile(const std::string& path)
{
    SCOPED_PROFILE_ZONE();

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

    auto optionallyParseShadowMapSize = [](const json& jsonLight, Light& light) {
        if (jsonLight.find("shadowMapSize") != jsonLight.end()) {
            int mapSize[2];
            jsonLight.at("shadowMapSize").get_to(mapSize);
            light.setShadowMapSize({ mapSize[0], mapSize[1] });
        }
    };

    auto optionallyParseLightName = [](const json& jsonLight, Light& light) {
        if (jsonLight.find("name") != jsonLight.end())
            light.setName(jsonLight.at("name"));
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

            optionallyParseShadowMapSize(jsonLight, *light);
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

            optionallyParseShadowMapSize(jsonLight, *light);
            optionallyParseLightName(jsonLight, *light);

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

        // TODO: For now always just make FpsCamera objects. Later we probably want to be able to change etc.
        // E.g. make a camera controller class which wraps or refers to a Camera object.
        auto camera = std::make_unique<FpsCamera>();

        vec3 position = readVec3(jsonCamera.at("position"));
        vec3 direction = normalize(readVec3(jsonCamera.at("direction")));
        camera->lookAt(position, position + direction, moos::globalUp);

        if (jsonCamera.find("exposure") != jsonCamera.end()) {
            if (jsonCamera.at("exposure") == "manual") {
                camera->useAutomaticExposure = false;
                camera->iso = jsonCamera.at("ISO");
                camera->aperture = jsonCamera.at("aperture");
                camera->shutterSpeed = 1.0f / jsonCamera.at("shutter");
            } else if (jsonCamera.at("exposure") == "auto") {
                camera->useAutomaticExposure = true;
                camera->exposureCompensation = jsonCamera.at("EC");
                camera->adaptionRate = jsonCamera.at("adaptionRate");
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

    rebuildGpuSceneData();
    m_sceneDataNeedsRebuild = false;
}

void Scene::saveCameraToClipboard(const Camera& camera)
{
    std::vector<float> cameraPosition = { camera.position().x,
                                          camera.position().y,
                                          camera.position().z };
    std::vector<float> cameraDirection = { camera.forward().x,
                                           camera.forward().y,
                                           camera.forward().z };

    nlohmann::json jsonCamera {};

    jsonCamera["name"] = "copied-camera";
    jsonCamera["position"] = cameraPosition;
    jsonCamera["direction"] = cameraDirection;
    if (camera.useAutomaticExposure) {
        jsonCamera["exposure"] = "auto";
        jsonCamera["adaptionRate"] = camera.adaptionRate;
        jsonCamera["EC"] = camera.exposureCompensation;
    } else {
        jsonCamera["exposure"] = "manual";
        jsonCamera["aperture"] = camera.aperture;
        jsonCamera["shutter"] = 1.0f / camera.shutterSpeed;
        jsonCamera["ISO"] = camera.iso;
    }

    std::string jsonString = jsonCamera.dump(0);
    glfwSetClipboardString(nullptr, jsonString.c_str());
}
