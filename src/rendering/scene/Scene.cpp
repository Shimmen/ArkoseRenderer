#include "Scene.h"

#include "backend/Resources.h"
#include "rendering/Registry.h"
#include "rendering/scene/models/GltfModel.h"
#include "utility/FileIO.h"
#include "utility/Logging.h"
#include <fstream>
#include <imgui.h>
#include <moos/aabb.h>
#include <moos/transform.h>
#include <nlohmann/json.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

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
        vec3 position = readVec3(jsonCamera.at("position"));
        vec3 direction = normalize(readVec3(jsonCamera.at("direction")));
        camera.lookAt(position, position + direction, moos::globalUp);

        if (jsonCamera.find("exposure") != jsonCamera.end()) {
            if (jsonCamera.at("exposure") == "manual") {
                camera.useAutomaticExposure = false;
                camera.iso = jsonCamera.at("ISO");
                camera.aperture = jsonCamera.at("aperture");
                camera.shutterSpeed = 1.0f / jsonCamera.at("shutter");
            } else if (jsonCamera.at("exposure") == "auto") {
                camera.useAutomaticExposure = true;
                camera.exposureCompensation = jsonCamera.at("EC");
                camera.adaptionRate = jsonCamera.at("adaptionRate");
            }
        }

        std::string name = jsonCamera.at("name");
        m_allCameras[name] = camera;
    }

    std::string mainCamera = jsonScene.at("camera");
    auto entry = m_allCameras.find(mainCamera);
    if (entry != m_allCameras.end()) {
        m_currentMainCamera = m_allCameras[mainCamera];
    }

    rebuildGpuSceneData();
    m_sceneDataNeedsRebuild = false;
}

void Scene::update(float elapsedTime, float deltaTime)
{
    camera().update(Input::instance(), mainViewportSize(), deltaTime);

    if (m_sceneDataNeedsRebuild) {
        // We shouldn't need to rebuild the whole thing, just append and potentially remove some stuff.. But the
        // distinction here is that it wouldn't be enough to just update some matrices, e.g. if an object was moved
        // If we save the vector of textures & materials we can probably resuse a lot of calculations. There is no
        // rush with that though, as currently we can't even make changes that would require a rebuild..
        rebuildGpuSceneData();
        m_sceneDataNeedsRebuild = false;
    }

    if (camera().useAutomaticExposure) {
        // TODO: Implement soon!
        ASSERT_NOT_REACHED();
    } else {
        // See camera.glsl for reference
        float ev100 = std::log2((camera().aperture * camera().aperture) / camera().shutterSpeed * 100.0 / camera().iso);
        float maxLuminance = 1.2f * std::pow(2.0f, ev100);
        m_lightPreExposure = 1.0f / maxLuminance;
    }

    ImGui::Begin("Scene");
    {
        if (ImGui::TreeNode("Metainfo")) {
            ImGui::Text("Number of managed resources:");
            ImGui::Columns(3);
            ImGui::Text("meshes: %u", meshCount());
            ImGui::NextColumn();
            ImGui::Text("materials: %u", m_usedMaterials.size());
            ImGui::NextColumn();
            ImGui::Text("textures: %u", m_usedTextures.size());
            ImGui::Columns(1);
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Lighting")) {
            ImGui::ColorEdit3("Sun color", value_ptr(sun().color));
            ImGui::SliderFloat("Sun illuminance (lx)", &sun().illuminance, 1.0f, 150000.0f);
            ImGui::SliderFloat("Ambient (lx)", &ambient(), 0.0f, 1000.0f);
            ImGui::TreePop();
        }
        if (ImGui::Button("Current camera to clipboard")) {
            const auto& camera = m_currentMainCamera;
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
    }
    ImGui::End();
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

DrawCallDescription Scene::fitVertexAndIndexDataForMesh(Badge<Mesh>, const Mesh& mesh, const VertexLayout& layout, std::optional<DrawCallDescription> alignWith)
{
    const size_t initialIndexBufferSize = 100'000 * sizeof(uint32_t);
    const size_t initialVertexBufferSize = 50'000 * layout.packedVertexSize();

    bool doAlign = alignWith.has_value();
    ASSERT(alignWith->sourceMesh == &mesh);

    std::vector<uint8_t> vertexData = mesh.vertexData(layout);

    auto entry = m_globalVertexBuffers.find(layout);
    if (entry == m_globalVertexBuffers.end()) {

        size_t offset = doAlign ? (alignWith->vertexOffset * layout.packedVertexSize()) : 0;
        size_t minRequiredBufferSize = offset + vertexData.size();

        Buffer& buffer = m_registry.createBuffer(std::max(initialVertexBufferSize, minRequiredBufferSize), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal);
        buffer.setName("SceneVertexBuffer");

        m_globalVertexBuffers[layout] = &buffer;
    }

    Buffer& vertexBuffer = *m_globalVertexBuffers[layout];
    size_t newDataStartOffset = doAlign
        ? alignWith->vertexOffset * layout.packedVertexSize()
        : m_nextFreeVertexIndex * layout.packedVertexSize();

    vertexBuffer.updateDataAndGrowIfRequired(vertexData.data(), vertexData.size(), newDataStartOffset);

    if (doAlign) {
        // TODO: Maybe ensure we haven't already fitted this mesh+layout combo and is just overwriting at this point. Well, before doing it I guess..
        DrawCallDescription reusedDrawCall = alignWith.value();
        reusedDrawCall.vertexBuffer = m_globalVertexBuffers[layout];
        return reusedDrawCall;
    }

    int vertexCount = mesh.vertexCountForLayout(layout);
    int vertexOffset = m_nextFreeVertexIndex;
    m_nextFreeVertexIndex += vertexCount;


    DrawCallDescription drawCall {};
    drawCall.sourceMesh = &mesh;

    drawCall.vertexBuffer = &vertexBuffer;
    drawCall.vertexCount = vertexCount;
    drawCall.vertexOffset = vertexOffset;

    // Fit index data
    {
        std::vector<uint32_t> indexData = mesh.indexData();
        size_t requiredAdditionalSize = indexData.size() * sizeof(uint32_t);

        if (m_global32BitIndexBuffer == nullptr) {
            m_global32BitIndexBuffer = &m_registry.createBuffer(std::max(initialIndexBufferSize, requiredAdditionalSize), Buffer::Usage::Index, Buffer::MemoryHint::GpuOptimal);
            m_global32BitIndexBuffer->setName("SceneIndexBuffer");
        }

        int firstIndex = m_nextFreeIndex;
        m_nextFreeIndex += indexData.size();

        m_global32BitIndexBuffer->updateDataAndGrowIfRequired(indexData.data(), requiredAdditionalSize, firstIndex * sizeof(uint32_t));

        drawCall.indexBuffer = m_global32BitIndexBuffer;
        drawCall.indexCount = indexData.size();
        drawCall.indexType = IndexType::UInt32;
        drawCall.firstIndex = firstIndex;
    }

    return drawCall;
}

Buffer& Scene::globalVertexBufferForLayout(const VertexLayout& layout) const
{
    auto entry = m_globalVertexBuffers.find(layout);
    if (entry == m_globalVertexBuffers.end())
        LogErrorAndExit("Can't get vertex buffer for layout since it has not been created! Please ensureDrawCallIsAvailable for at least one mesh before calling this.\n");
    return *entry->second;
}

Buffer& Scene::globalIndexBuffer() const
{
    if (m_global32BitIndexBuffer == nullptr)
        LogErrorAndExit("Can't get global index buffer since it has not been created! Please ensureDrawCallIsAvailable for at least one indexed mesh before calling this.\n");
    return *m_global32BitIndexBuffer;
}

IndexType Scene::globalIndexBufferType() const
{
    // For simplicity we keep a single 32-bit index buffer, since every mesh should fit in there.
    return IndexType::UInt32;
}

constexpr bool operator==(const ShaderMaterial& lhs, const ShaderMaterial& rhs)
{
    if (lhs.baseColor != rhs.baseColor)
        return false;
    if (lhs.normalMap != rhs.normalMap)
        return false;
    if (lhs.metallicRoughness != rhs.metallicRoughness)
        return false;
    if (lhs.emissive != rhs.emissive)
        return false;
    return true;
}

void Scene::rebuildGpuSceneData()
{
    m_usedTextures.clear();
    m_usedMaterials.clear();

    std::unordered_map<Texture*, int> textureIndices;
    auto pushTexture = [&](Texture* texture) -> int {
        auto entry = textureIndices.find(texture);
        if (entry != textureIndices.end())
            return entry->second;

        int textureIndex = static_cast<int>(m_usedTextures.size());
        textureIndices[texture] = textureIndex;
        m_usedTextures.push_back(texture);

        return textureIndex;
    };

    auto pushMaterial = [&](ShaderMaterial shaderMaterial) -> int {
        // Would be nice if we could hash them..
        for (int idx = 0; idx < m_usedMaterials.size(); ++idx) {
            if (m_usedMaterials[idx] == shaderMaterial)
                return idx;
        }

        int materialIndex = static_cast<int>(m_usedMaterials.size());
        m_usedMaterials.push_back(shaderMaterial);

        return materialIndex;
    };

    int numMeshes = forEachMesh([&](size_t meshIdx, Mesh& mesh) {

        Material& material = mesh.material();
        int materialIndex = pushMaterial(ShaderMaterial {
            .baseColor = pushTexture(material.baseColorTexture()),
            .normalMap = pushTexture(material.normalMapTexture()),
            .metallicRoughness = pushTexture(material.metallicRoughnessTexture()),
            .emissive = pushTexture(material.emissiveTexture()),
        });

        ASSERT(materialIndex >= 0);
        mesh.setMaterialIndex({}, materialIndex);

    });

    if (m_usedTextures.size() > SCENE_MAX_TEXTURES) {
        LogErrorAndExit("Scene: we need to up the number of max textures that can be handled by the scene! We have %u, the capacity is %u.\n",
                        m_usedTextures.size(), SCENE_MAX_TEXTURES);
    }

    // Create material buffer
    size_t materialBufferSize = m_usedMaterials.size() * sizeof(ShaderMaterial);
    m_materialDataBuffer = &m_registry.createBuffer(materialBufferSize, Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal);
    m_materialDataBuffer->updateData(m_usedMaterials.data(), materialBufferSize);
    m_materialDataBuffer->setName("SceneMaterialData");

    m_materialBindingSet = &m_registry.createBindingSet({ { 0, ShaderStageFragment, m_materialDataBuffer },
                                                          { 1, ShaderStageFragment, m_usedTextures, SCENE_MAX_TEXTURES } });
}

BindingSet& Scene::globalMaterialBindingSet() const
{
    ASSERT(m_materialBindingSet);
    return *m_materialBindingSet;
}
