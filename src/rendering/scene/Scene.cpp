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

    rebuildGpuSceneData();
    m_sceneDataNeedsRebuild = false;
}

void Scene::update(float elapsedTime, float deltaTime)
{
    // TODO: Update the main camera and remove that code from the apps
    //camera().update(Input::instance(), GlobalState::get().windowExtent(), deltaTime)
    // This would also be a good place to e.g. update animations that are set up

    if (m_sceneDataNeedsRebuild) {
        // We shouldn't need to rebuild the whole thing, just append and potentially remove some stuff.. But the
        // distinction here is that it wouldn't be enough to just update some matrices, e.g. if an object was moved
        // If we save the vector of textures & materials we can probably resuse a lot of calculations. There is no
        // rush with that though, as currently we can't even make changes that would require a rebuild..
        rebuildGpuSceneData();
        m_sceneDataNeedsRebuild = false;
    }

    /*
    ImGui::Begin("SCENE");
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
    }
    ImGui::End();
    */
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

DrawCall Scene::fitVertexAndIndexDataForMesh(Badge<Mesh>, const Mesh& mesh, const VertexLayout& layout)
{
    // TODO: Maybe ensure we haven't already fitted this mesh+layout combo?

    DrawCall drawCall {};
    drawCall.type = DrawCall::Type::Indexed;

    // Fit index data
    {
        std::vector<uint32_t> indexData = mesh.indexData();
        size_t requiredAdditionalSize = indexData.size() * sizeof(uint32_t);

        ResizableBuffer& indexBuffer = m_global32BitIndexBuffer;
        if (indexBuffer.buffer == nullptr)
            indexBuffer.buffer = &m_registry.createBuffer(std::max(InitialIndexBufferSize, requiredAdditionalSize), Buffer::Usage::Index, Buffer::MemoryHint::GpuOptimal);

        size_t remainingSize = indexBuffer.buffer->size() - indexBuffer.offsetToNextFree;

        if (requiredAdditionalSize > remainingSize) {
            size_t currentSize = indexBuffer.buffer->size();
            size_t newSize = std::max(2 * currentSize, currentSize + requiredAdditionalSize);
            indexBuffer.buffer->reallocateWithSize(newSize, Buffer::ReallocateStrategy::CopyExistingData);
        }

        int firstIndex = indexBuffer.offsetToNextFree / sizeof(uint32_t);
        indexBuffer.buffer->updateData(indexData.data(), requiredAdditionalSize, indexBuffer.offsetToNextFree);
        indexBuffer.offsetToNextFree += requiredAdditionalSize;

        drawCall.indexBuffer = indexBuffer.buffer;
        drawCall.indexCount = indexData.size();
        drawCall.indexType = IndexType::UInt32;
        drawCall.firstIndex = firstIndex;
    }

    // Fit vertex data
    {
        std::vector<uint8_t> vertexData = mesh.vertexData(layout);
        size_t requiredAdditionalSize = vertexData.size();

        auto entry = m_globalVertexBuffers.find(layout);
        if (entry == m_globalVertexBuffers.end()) {
            auto buffer = std::make_unique<ResizableBuffer>();
            buffer->buffer = &m_registry.createBuffer(std::max(InitialVertexBufferSize, requiredAdditionalSize), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal);
            m_globalVertexBuffers[layout] = std::move(buffer);
        }

        ResizableBuffer& vertexBuffer = *m_globalVertexBuffers[layout];

        size_t remainingSize = vertexBuffer.buffer->size() - vertexBuffer.offsetToNextFree;
        if (requiredAdditionalSize > remainingSize) {
            size_t currentSize = vertexBuffer.buffer->size();
            size_t newSize = std::max(2 * currentSize, currentSize + requiredAdditionalSize);
            vertexBuffer.buffer->reallocateWithSize(newSize, Buffer::ReallocateStrategy::CopyExistingData);
        }

        int vertexOffset = vertexBuffer.offsetToNextFree / layout.packedVertexSize();
        vertexBuffer.buffer->updateData(vertexData.data(), requiredAdditionalSize, vertexBuffer.offsetToNextFree);
        vertexBuffer.offsetToNextFree += requiredAdditionalSize;

        drawCall.vertexBuffer = vertexBuffer.buffer;
        drawCall.vertexCount = mesh.vertexCountForLayout(layout);
        drawCall.vertexOffset = vertexOffset;
    }

    return drawCall;
}

Buffer& Scene::globalVertexBufferForLayout(const VertexLayout& layout) const
{
    auto entry = m_globalVertexBuffers.find(layout);
    if (entry == m_globalVertexBuffers.end())
        LogErrorAndExit("Can't get vertex buffer for layout since it has not been created! Please ensureDrawCall for at least one mesh before calling this.\n");
    return *entry->second->buffer;
}

Buffer& Scene::globalIndexBuffer() const
{
    if (m_global32BitIndexBuffer.buffer == nullptr)
        LogErrorAndExit("Can't get global index buffer since it has not been created! Please ensureDrawCall for at least one indexed mesh before calling this.\n");
    return *m_global32BitIndexBuffer.buffer;
}

IndexType Scene::globalIndexBufferType() const
{
    // For simplicity we keep a single 32-bit index buffer, since every mesh should fit in there.
    return IndexType::UInt32;
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

    if (numMeshes > SCENE_MAX_DRAWABLES) {
        // TODO: Or use a storage buffer instead..
        LogErrorAndExit("Scene: we need to up the number of max drawables that can be handled by the scene! We have %u, the capacity is %u.\n", numMeshes, SCENE_MAX_DRAWABLES);
    }

    if (m_usedTextures.size() > SCENE_MAX_TEXTURES) {
        LogErrorAndExit("Scene: we need to up the number of max textures that can be handled by the scene! We have %u, the capacity is %u.\n",
                        m_usedTextures.size(), SCENE_MAX_TEXTURES);
    }

    // Create material buffer
    // TODO: Support changing materials! I.e. update this buffer when data has changed..
    // TODO: Use shader storage buffer instead! Then we won't have an upper cap!
    if (m_usedMaterials.size() > SCENE_MAX_MATERIALS) {
        LogErrorAndExit("Scene: we need to up the number of max materials that can be handled by the scene! We have %u, the capacity is %u.\n",
                        m_usedMaterials.size(), SCENE_MAX_MATERIALS);
    }
    size_t materialBufferSize = m_usedMaterials.size() * sizeof(ShaderMaterial);
    m_materialDataBuffer = &m_registry.createBuffer(materialBufferSize, Buffer::Usage::UniformBuffer, Buffer::MemoryHint::GpuOptimal);
    m_materialDataBuffer->updateData(m_usedMaterials.data(), materialBufferSize);
    m_materialDataBuffer->setName("SceneMaterialData");
}

Buffer& Scene::globalMaterialBuffer() const
{
    ASSERT(m_materialDataBuffer);
    return *m_materialDataBuffer;
}

const std::vector<Texture*>& Scene::globalTextureArray()
{
    return m_usedTextures;
}