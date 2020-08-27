#include "GltfModel.h"

#include "utility/FileIO.h"
#include "utility/Logging.h"
#include <mooslib/transform.h>
#include <string>
#include <unordered_map>

static std::unordered_map<std::string, tinygltf::Model> s_loadedModels {};

std::unique_ptr<Model> GltfModel::load(const std::string& path)
{
    if (!FileIO::isFileReadable(path)) {
        LogError("Could not find glTF model file at path '%s'\n", path.c_str());
        return nullptr;
    }

    auto entry = s_loadedModels.find(path);
    if (entry != s_loadedModels.end()) {
        tinygltf::Model& internal = s_loadedModels[path];
        return std::make_unique<GltfModel>(path, internal);
    }

    tinygltf::TinyGLTF loader {};
    tinygltf::Model& internal = s_loadedModels[path];

    std::string error;
    std::string warning;

    // TODO: Check if ASCII or binary!
    bool result = loader.LoadASCIIFromFile(&internal, &error, &warning, path);

    if (!warning.empty()) {
        LogWarning("glTF loader warning: %s\n", warning.c_str());
    }

    if (!error.empty()) {
        LogError("glTF loader error: %s\n", error.c_str());
    }

    if (!result) {
        LogError("glTF loader: could not load file '%s'\n", path.c_str());
        return nullptr;
    }

    if (internal.defaultScene == -1 && internal.scenes.size() > 1) {
        LogWarning("glTF loader: scene ambiguity in model '%s'\n", path.c_str());
    }

    return std::make_unique<GltfModel>(path, internal);
}

GltfModel::GltfModel(std::string path, const tinygltf::Model& model)
    : m_path(std::move(path))
    , m_model(&model)
{
    const tinygltf::Scene& scene = (m_model->defaultScene != -1)
        ? m_model->scenes[m_model->defaultScene]
        : m_model->scenes.front();

    auto createMatrix = [](const tinygltf::Node& node) -> mat4 {
        if (!node.matrix.empty()) {
            const auto& vals = node.matrix;
            ASSERT(vals.size() == 16);
            return mat4(vec4(vals[0], vals[1], vals[2], vals[3]),
                        vec4(vals[4], vals[5], vals[6], vals[7]),
                        vec4(vals[8], vals[9], vals[10], vals[11]),
                        vec4(vals[12], vals[13], vals[14], vals[15]));
        } else {
            mat4 translation = node.translation.empty()
                ? mat4(1.0f)
                : moos::translate(vec3(node.translation[0], node.translation[1], node.translation[2]));
            mat4 rotation = node.rotation.empty()
                ? mat4(1.0f)
                : moos::rotate(quat(vec3(node.rotation[0], node.rotation[1], node.rotation[2]), node.rotation[3]));
            mat4 scale = node.scale.empty()
                ? mat4(1.0f)
                : moos::scale(vec3(node.scale[0], node.scale[1], node.scale[2]));
            return translation * rotation * scale;
        }
    };

    std::function<void(const tinygltf::Node&, mat4)> findMeshesRecursively = [&](const tinygltf::Node& node, mat4 matrix) {
        matrix = matrix * createMatrix(node);

        if (node.mesh != -1) {
            auto& mesh = m_model->meshes[node.mesh];
            for (int i = 0; i < mesh.primitives.size(); ++i) {

                std::string meshName = mesh.name;
                if (mesh.primitives.size() > 1) {
                    meshName += "_" + std::to_string(i);
                }

                auto gltfMesh = std::make_unique<GltfMesh>(meshName, this, model, mesh.primitives[i], matrix);
                gltfMesh->setModel(this);

                m_meshes.push_back(std::move(gltfMesh));
            }
        }

        for (int childIdx : node.children) {
            auto& child = model.nodes[childIdx];
            findMeshesRecursively(child, matrix);
        }
    };

    for (int nodeIdx : scene.nodes) {
        auto& node = model.nodes[nodeIdx];
        findMeshesRecursively(node, mat4(1.0f));
    }
}

size_t GltfModel::meshCount() const
{
    return m_meshes.size();
}

void GltfModel::forEachMesh(std::function<void(Mesh&)> callback)
{
    for (auto& mesh : m_meshes) {
        callback(*mesh);
    }
}

void GltfModel::forEachMesh(std::function<void(const Mesh&)> callback) const
{
    for (auto& mesh : m_meshes) {
        callback(*mesh);
    }
}

std::string GltfModel::directory() const
{
    int lastSlash = m_path.rfind('/');
    if (lastSlash == -1) {
        lastSlash = m_path.rfind('\\');
        if (lastSlash == -1) {
            return "";
        }
    }
    auto dir = m_path.substr(0, lastSlash + 1);
    return dir;
}

GltfMesh::GltfMesh(std::string name, const GltfModel* parent, const tinygltf::Model& model, const tinygltf::Primitive& primitive, mat4 matrix)
    : Mesh(Transform(matrix, &parent->transform()))
    , m_name(std::move(name))
    , m_parentModel(parent)
    , m_model(&model)
    , m_primitive(&primitive)
{
    if (primitive.mode != TINYGLTF_MODE_TRIANGLES) {
        LogErrorAndExit("glTF mesh: primitive with mode other than triangles is not yet supported\n");
    }

    // TODO: Add bounding boxes so we can do sorting, etc.
    //meshInfo.aabb = mathkit::aabb(position.minValues, position.maxValues);
}

std::unique_ptr<Material> GltfMesh::createMaterial()
{
    auto& gltfMaterial = m_model->materials[m_primitive->material];

    auto textureUri = [&](int texIndex, std::string defaultPath) -> std::string {
        if (texIndex == -1) {
            return defaultPath;
        }
        auto& texture = m_model->textures[texIndex];
        auto& image = m_model->images[texture.source];
        if (!image.uri.empty()) {
            return m_parentModel->directory() + image.uri;
        } else {
            return defaultPath;
        }
    };

    auto material = std::make_unique<Material>();
    material->setMesh(this);

    if (gltfMaterial.pbrMetallicRoughness.baseColorTexture.index != -1) {
        material->baseColor = textureUri(gltfMaterial.pbrMetallicRoughness.baseColorTexture.index, "assets/default-baseColor.png");
    }
    std::vector<double> c = gltfMaterial.pbrMetallicRoughness.baseColorFactor;
    material->baseColorFactor = vec4(c[0], c[1], c[2], c[3]);

    material->normalMap = textureUri(gltfMaterial.normalTexture.index, "assets/default-normal.png");
    material->metallicRoughness = textureUri(gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index, "assets/default-black.png");
    material->emissive = textureUri(gltfMaterial.emissiveTexture.index, "assets/default-black.png");

    return material;
}

const tinygltf::Accessor* GltfMesh::getAccessor(const char* name) const
{
    auto entry = m_primitive->attributes.find(name);
    if (entry == m_primitive->attributes.end()) {
        LogError("glTF mesh: primitive is missing attribute of name '%s'\n", name);
        return nullptr;
    }
    return &m_model->accessors[entry->second];
}

const std::vector<vec3>& GltfMesh::positionData() const
{
    if (m_positionData.has_value())
        return m_positionData.value();

    const tinygltf::Accessor& accessor = *getAccessor("POSITION");
    ASSERT(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
    ASSERT(accessor.type == TINYGLTF_TYPE_VEC3);

    const tinygltf::BufferView& view = m_model->bufferViews[accessor.bufferView];
    ASSERT(view.byteStride == 0 || view.byteStride == 12); // (i.e. tightly packed)

    const tinygltf::Buffer& buffer = m_model->buffers[view.buffer];

    const unsigned char* start = buffer.data.data() + view.byteOffset + accessor.byteOffset;
    auto* first = reinterpret_cast<const vec3*>(start);

    m_positionData = std::vector<vec3>(first, first + accessor.count);
    return m_positionData.value();
}

const std::vector<vec2>& GltfMesh::texcoordData() const
{
    if (m_texcoordData.has_value())
        return m_texcoordData.value();

    const tinygltf::Accessor* accessor = getAccessor("TEXCOORD_0");
    if (accessor == nullptr) {
        m_texcoordData = std::vector<vec2>();
        return m_texcoordData.value();
    }

    ASSERT(accessor->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
    ASSERT(accessor->type == TINYGLTF_TYPE_VEC2);

    const tinygltf::BufferView& view = m_model->bufferViews[accessor->bufferView];
    ASSERT(view.byteStride == 0 || view.byteStride == 8); // (i.e. tightly packed)

    const tinygltf::Buffer& buffer = m_model->buffers[view.buffer];

    const unsigned char* start = buffer.data.data() + view.byteOffset + accessor->byteOffset;
    auto* first = reinterpret_cast<const vec2*>(start);

    m_texcoordData = std::vector<vec2>(first, first + accessor->count);
    return m_texcoordData.value();
}

const std::vector<vec3>& GltfMesh::normalData() const
{
    if (m_normalData.has_value())
        return m_normalData.value();

    const tinygltf::Accessor* accessor = getAccessor("NORMAL");
    if (accessor == nullptr) {
        m_normalData = std::vector<vec3>();
        return m_normalData.value();
    }

    ASSERT(accessor->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
    ASSERT(accessor->type == TINYGLTF_TYPE_VEC3);

    const tinygltf::BufferView& view = m_model->bufferViews[accessor->bufferView];
    ASSERT(view.byteStride == 0 || view.byteStride == 12); // (i.e. tightly packed)

    const tinygltf::Buffer& buffer = m_model->buffers[view.buffer];

    const unsigned char* start = buffer.data.data() + view.byteOffset + accessor->byteOffset;
    auto* first = reinterpret_cast<const vec3*>(start);

    m_normalData = std::vector<vec3>(first, first + accessor->count);
    return m_normalData.value();
    ;
}

const std::vector<vec4>& GltfMesh::tangentData() const
{
    if (m_tangentData.has_value())
        return m_tangentData.value();

    const tinygltf::Accessor* accessor = getAccessor("TANGENT");
    if (accessor == nullptr) {
        m_tangentData = std::vector<vec4>();
        return m_tangentData.value();
    }

    ASSERT(accessor->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
    ASSERT(accessor->type == TINYGLTF_TYPE_VEC4);

    const tinygltf::BufferView& view = m_model->bufferViews[accessor->bufferView];
    ASSERT(view.byteStride == 0 || view.byteStride == 16); // (i.e. tightly packed)

    const tinygltf::Buffer& buffer = m_model->buffers[view.buffer];

    const unsigned char* start = buffer.data.data() + view.byteOffset + accessor->byteOffset;
    auto* first = reinterpret_cast<const vec4*>(start);

    m_tangentData = std::vector<vec4>(first, first + accessor->count);
    return m_tangentData.value();
}

const std::vector<uint32_t>& GltfMesh::indexData() const
{
    if (m_indexData.has_value())
        return m_indexData.value();

    ASSERT(isIndexed());
    const tinygltf::Accessor& accessor = m_model->accessors[m_primitive->indices];
    ASSERT(accessor.type == TINYGLTF_TYPE_SCALAR);

    const tinygltf::BufferView& view = m_model->bufferViews[accessor.bufferView];
    ASSERT(view.byteStride == 0); // (i.e. tightly packed)

    const tinygltf::Buffer& buffer = m_model->buffers[view.buffer];
    const unsigned char* start = buffer.data.data() + view.byteOffset + accessor.byteOffset;

    std::vector<uint32_t> vec;
    vec.reserve(accessor.count);

    switch (accessor.componentType) {
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
        auto* first = reinterpret_cast<const uint8_t*>(start);
        for (size_t i = 0; i < accessor.count; ++i) {
            uint32_t val = (uint32_t)(*(first + i));
            vec.emplace_back(val);
        }
        break;
    }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
        auto* first = reinterpret_cast<const uint16_t*>(start);
        for (size_t i = 0; i < accessor.count; ++i) {
            uint32_t val = (uint32_t)(*(first + i));
            vec.emplace_back(val);
        }
        break;
    }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
        auto* first = reinterpret_cast<const uint32_t*>(start);
        for (size_t i = 0; i < accessor.count; ++i) {
            uint32_t val = *(first + i);
            vec.emplace_back(val);
        }
        break;
    }
    default:
        ASSERT_NOT_REACHED();
    }

    m_indexData = std::move(vec);
    return m_indexData.value();
}

size_t GltfMesh::indexCount() const
{
    ASSERT(isIndexed());
    const tinygltf::Accessor& accessor = m_model->accessors[m_primitive->indices];
    ASSERT(accessor.type == TINYGLTF_TYPE_SCALAR);

    return accessor.count;
}

bool GltfMesh::isIndexed() const
{
    return m_primitive->indices != -1;
}

IndexType GltfMesh::indexType() const
{
    return IndexType::UInt32;
}
