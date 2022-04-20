#include "Scene.h"

#include "core/Assert.h"
#include "rendering/camera/Camera.h"
#include "rendering/camera/FpsCamera.h"
#include "rendering/scene/GpuScene.h"
#include "rendering/scene/models/GltfModel.h"
#include "utility/FileIO.h"
#include <imgui.h>
#include <ImGuizmo.h>
#include <nlohmann/json.hpp>
#include <fstream>

Scene::Scene(Backend& backend, Extent2D initialMainViewportSize)
{
    m_gpuScene = std::make_unique<GpuScene>(*this, backend, initialMainViewportSize);
}

Scene::~Scene()
{
}

void Scene::newFrame(Extent2D mainViewportSize, bool firstFrame)
{
    camera().newFrame({}, mainViewportSize, firstFrame);

    // NOTE: We only want to do this on leaf-nodes right now, i.e. meshes not models.
    forEachModel([&](size_t modelIdx, Model& model) {
        model.forEachMesh([&](Mesh& mesh) {
            mesh.transform().newFrame({}, firstFrame);
        });
    });
}

void Scene::update(float elapsedTime, float deltaTime)
{
    drawSceneGui();
    drawSceneGizmos();
    gpuScene().drawGui();
}

void Scene::setupFromDescription(const Description& description)
{
    // NOTE: Must initialize GPU scene before we start registering meshes etc.
    gpuScene().initialize({}, description.maintainRayTracingScene);

    if (!FileIO::isFileReadable(description.path))
        ARKOSE_LOG(Fatal, "Could not read scene file '{}', exiting", description.path);
    loadFromFile(description.path);
}

Model& Scene::addModel(std::unique_ptr<Model> model)
{
    ARKOSE_ASSERT(model);
    m_models.push_back(std::move(model));
    Model& addedModel = *m_models.back();

    addedModel.forEachMesh([&](Mesh& mesh) {
        gpuScene().registerMesh(mesh);
    });

    return addedModel;
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

void Scene::setEnvironmentMap(EnvironmentMap& environmentMap)
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

    moos::aabb3 sceneBox {};
    scene().forEachMesh([&](size_t, Mesh& mesh) {
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
    EnvironmentMap envMap;
    envMap.assetPath = jsonEnv.at("texture");
    envMap.brightnessFactor = jsonEnv.at("illuminance");
    setEnvironmentMap(envMap);

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
}

void Scene::drawSceneGui()
{
    ImGui::Begin("Scene");

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

    ImGui::Separator();

    if (ImGui::TreeNode("Exposure control")) {
        camera().renderExposureGUI();
        ImGui::TreePop();
    }

    ImGui::End();
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

    if (selectedModel()) {

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

        mat4 matrix = selectedModel()->transform().localMatrix();
        ImGuizmo::Manipulate(value_ptr(viewMatrix), value_ptr(projMatrix), operation, mode, value_ptr(matrix));
        selectedModel()->transform().setLocalMatrix(matrix);
    }
}
