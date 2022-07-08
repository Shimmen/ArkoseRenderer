#include "GltfModel.h"

#include "core/Logging.h"
#include "utility/FileIO.h"
#include "utility/Image.h"
#include "utility/Profiling.h"
#include <ark/transform.h>
#include <string>
#include <unordered_map>

static std::unordered_map<std::string, tinygltf::Model> s_loadedModels {};

std::unique_ptr<Model> GltfModel::load(const std::string& path)
{
    SCOPED_PROFILE_ZONE();

    if (!FileIO::isFileReadable(path)) {
        ARKOSE_LOG(Error, "Could not find glTF model file at path '{}'", path);
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

    bool result = false;
    {
        SCOPED_PROFILE_ZONE_NAMED("TinyGLTF work");
        if (path.ends_with(".gltf")) {
            result = loader.LoadASCIIFromFile(&internal, &error, &warning, path);
        } else if (path.ends_with(".glb")) {
            result = loader.LoadBinaryFromFile(&internal, &error, &warning, path);
        }
    }

    if (!warning.empty()) {
        ARKOSE_LOG(Warning, "glTF loader warning: {}", warning);
    }

    if (!error.empty()) {
        ARKOSE_LOG(Error, "glTF loader error: {}", error);
    }

    if (!result) {
        ARKOSE_LOG(Error, "glTF loader: could not load file '{}'", path);
        return nullptr;
    }

    if (internal.defaultScene == -1 && internal.scenes.size() > 1) {
        ARKOSE_LOG(Warning, "glTF loader: scene ambiguity in model '{}'", path);
    }

    return std::make_unique<GltfModel>(path, internal);
}

GltfModel::GltfModel(std::string path, const tinygltf::Model& model)
    : m_path(std::move(path))
    , m_model(&model)
{
    SCOPED_PROFILE_ZONE();

    const tinygltf::Scene& scene = (m_model->defaultScene != -1)
        ? m_model->scenes[m_model->defaultScene]
        : m_model->scenes.front();

    auto createMatrix = [](const tinygltf::Node& node) -> mat4 {
        if (!node.matrix.empty()) {
            const auto& vals = node.matrix;
            ARKOSE_ASSERT(vals.size() == 16);
            return mat4(vec4((float)vals[0], (float)vals[1], (float)vals[2], (float)vals[3]),
                        vec4((float)vals[4], (float)vals[5], (float)vals[6], (float)vals[7]),
                        vec4((float)vals[8], (float)vals[9], (float)vals[10], (float)vals[11]),
                        vec4((float)vals[12], (float)vals[13], (float)vals[14], (float)vals[15]));
        } else {
            mat4 translation = node.translation.empty()
                ? mat4(1.0f)
                : ark::translate(vec3((float)node.translation[0], (float)node.translation[1], (float)node.translation[2]));
            mat4 rotation = node.rotation.empty()
                ? mat4(1.0f)
                : ark::rotate(quat(vec3((float)node.rotation[0], (float)node.rotation[1], (float)node.rotation[2]), (float)node.rotation[3]));
            mat4 scale = node.scale.empty()
                ? mat4(1.0f)
                : ark::scale(vec3((float)node.scale[0], (float)node.scale[1], (float)node.scale[2]));
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
    size_t lastSlash = m_path.rfind('/');
    if (lastSlash == std::string::npos) {
        lastSlash = m_path.rfind('\\');
        if (lastSlash == std::string::npos) {
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
    SCOPED_PROFILE_ZONE();

    if (primitive.mode != TINYGLTF_MODE_TRIANGLES) {
        ARKOSE_LOG(Fatal, "glTF mesh: primitive with mode other than triangles is not yet supported");
    }

    const tinygltf::Accessor& position = *getAccessor("POSITION");
    ARKOSE_ASSERT(position.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT && position.type == TINYGLTF_TYPE_VEC3);
    vec3 posMin = { (float)position.minValues[0], (float)position.minValues[1], (float)position.minValues[2] };
    vec3 posMax = { (float)position.maxValues[0], (float)position.maxValues[1], (float)position.maxValues[2] };
    m_aabb = ark::aabb3(posMin, posMax);

    vec3 center = (posMax + posMin) / 2.0f;
    float radius = length(posMax - posMin) / 2.0f;
    m_boundingSphere = geometry::Sphere(center, radius);
}

std::unique_ptr<Material> GltfMesh::createMaterial()
{
    SCOPED_PROFILE_ZONE();

    auto& gltfMaterial = m_model->materials[m_primitive->material];

    auto toTextureDesc = [&](int texIndex, bool sRGB, vec4 fallbackColor) -> Material::TextureDescription {

        if (texIndex == -1) {
            Material::TextureDescription desc {};
            desc.fallbackColor = fallbackColor;
            desc.sRGB = sRGB;
            return desc;
        }

        auto& texture = m_model->textures[texIndex];
        auto& sampler = m_model->samplers[texture.sampler];
        auto& image = m_model->images[texture.source];

        Material::TextureDescription desc;
        if (!image.uri.empty()) {
            desc = Material::TextureDescription(m_parentModel->directory() + image.uri);
        } else {

            Image::Info info;
            info.width = image.width;
            info.height = image.height;

            // TODO: Generalize!
            switch (image.pixel_type) {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                info.componentType = Image::ComponentType::UInt8;
                break;
            case TINYGLTF_COMPONENT_TYPE_FLOAT:
                info.componentType = Image::ComponentType::Float;
                break;
            default:
                ASSERT_NOT_REACHED();
            }

            switch (image.component) {
            case 1:
                info.pixelType = Image::PixelType::Grayscale;
                break;
            case 2:
                info.pixelType = Image::PixelType::RG;
                break;
            case 3:
                info.pixelType = Image::PixelType::RGB;
                break;
            case 4:
                info.pixelType = Image::PixelType::RGBA;
                break;
            }

            const tinygltf::BufferView& bufferView = m_model->bufferViews[image.bufferView];
            const tinygltf::Buffer& buffer = m_model->buffers[bufferView.buffer];

            size_t dataSize = bufferView.byteLength;
            const uint8_t* data = buffer.data.data() + bufferView.byteOffset;

            desc = Material::TextureDescription(Image(Image::MemoryType::EncodedImage, info, (void*)data, dataSize));
        }

        auto wrapModeFromTinyGltf = [](int filterMode) -> Texture::WrapMode {
            switch (filterMode) {
            case TINYGLTF_TEXTURE_WRAP_REPEAT:
                return Texture::WrapMode::Repeat;
            case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:
                return Texture::WrapMode::ClampToEdge;
            case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT:
                return Texture::WrapMode::MirroredRepeat;
            default:
                ASSERT_NOT_REACHED();
            }
        };

        desc.fallbackColor = fallbackColor;
        desc.sRGB = sRGB;

        desc.wrapMode.u = wrapModeFromTinyGltf(sampler.wrapS);
        desc.wrapMode.v = wrapModeFromTinyGltf(sampler.wrapT);
        desc.wrapMode.w = wrapModeFromTinyGltf(sampler.wrapR);

        switch (sampler.minFilter) {
        case TINYGLTF_TEXTURE_FILTER_NEAREST:
            desc.filters.min = Texture::MinFilter::Nearest;
            desc.mipmapped = false;
            break;
        case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
        case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
            // TODO: We need to change how we handle mipmapping for this to be correct..
            desc.filters.min = Texture::MinFilter::Nearest;
            desc.mipmapped = true;
            break;
        case TINYGLTF_TEXTURE_FILTER_LINEAR:
            desc.filters.min = Texture::MinFilter::Linear;
            desc.mipmapped = false;
            break;
        case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
        case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:
            // TODO: We need to change how we handle mipmapping for this to be correct..
            desc.filters.min = Texture::MinFilter::Linear;
            desc.mipmapped = true;
            break;
        case -1:
            // "glTF 2.0 spec does not define default value for `minFilter` and `magFilter`. Set -1 in TinyGLTF(issue #186)"
            desc.filters.min = Texture::MinFilter::Linear;
            desc.mipmapped = true;
            break;
        default:
            ASSERT_NOT_REACHED();
        }

        switch (sampler.magFilter) {
        case TINYGLTF_TEXTURE_FILTER_NEAREST:
            desc.filters.mag = Texture::MagFilter::Nearest;
            break;
        case TINYGLTF_TEXTURE_FILTER_LINEAR:
            desc.filters.mag = Texture::MagFilter::Linear;
            break;
        case -1:
            // "glTF 2.0 spec does not define default value for `minFilter` and `magFilter`. Set -1 in TinyGLTF(issue #186)"
            desc.filters.mag = Texture::MagFilter::Linear;
            break;
        default:
            ASSERT_NOT_REACHED();
        }

        return desc;
    };

    auto material = std::make_unique<Material>();

    if (gltfMaterial.alphaMode == "OPAQUE") {
        material->blendMode = Material::BlendMode::Opaque;
    } else if (gltfMaterial.alphaMode == "BLEND") {
        material->blendMode = Material::BlendMode::Translucent;
    } else if (gltfMaterial.alphaMode == "MASK") {
        material->blendMode = Material::BlendMode::Masked;
        material->maskCutoff = static_cast<float>(gltfMaterial.alphaCutoff);
    } else {
        ASSERT_NOT_REACHED();
    }

    std::vector<double> c = gltfMaterial.pbrMetallicRoughness.baseColorFactor;
    material->baseColorFactor = vec4((float)c[0], (float)c[1], (float)c[2], (float)c[3]);

    int baseColorIdx = gltfMaterial.pbrMetallicRoughness.baseColorTexture.index;
    material->baseColor = toTextureDesc(baseColorIdx, true, material->baseColorFactor);

    int normalMapIdx = gltfMaterial.normalTexture.index;
    material->normalMap = toTextureDesc(normalMapIdx, false, vec4(0.5f, 0.5f, 1.0f, 1.0f));

    int metallicRoughnessIdx = gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index;
    material->metallicRoughness = toTextureDesc(metallicRoughnessIdx, false, vec4(0.0f, 0.0f, 0.0f, 0.0f));

    int emissiveIdx = gltfMaterial.emissiveTexture.index;
    material->emissive = toTextureDesc(emissiveIdx, true, vec4(0.0f, 0.0f, 0.0f, 0.0f));

    return material;
}

const tinygltf::Accessor* GltfMesh::getAccessor(const char* name) const
{
    auto entry = m_primitive->attributes.find(name);
    if (entry == m_primitive->attributes.end()) {
        ARKOSE_LOG(Error, "glTF mesh: primitive is missing attribute of name '{}'", name);
        return nullptr;
    }
    return &m_model->accessors[entry->second];
}

const std::vector<vec3>& GltfMesh::positionData() const
{
    SCOPED_PROFILE_ZONE();

    if (m_positionData.has_value())
        return m_positionData.value();

    const tinygltf::Accessor& accessor = *getAccessor("POSITION");
    ARKOSE_ASSERT(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
    ARKOSE_ASSERT(accessor.type == TINYGLTF_TYPE_VEC3);

    const tinygltf::BufferView& view = m_model->bufferViews[accessor.bufferView];
    ARKOSE_ASSERT(view.byteStride == 0 || view.byteStride == 12); // (i.e. tightly packed)

    const tinygltf::Buffer& buffer = m_model->buffers[view.buffer];

    const unsigned char* start = buffer.data.data() + view.byteOffset + accessor.byteOffset;
    auto* first = reinterpret_cast<const vec3*>(start);

    m_positionData = std::vector<vec3>(first, first + accessor.count);
    return m_positionData.value();
}

const std::vector<vec2>& GltfMesh::texcoordData() const
{
    SCOPED_PROFILE_ZONE();

    if (m_texcoordData.has_value())
        return m_texcoordData.value();

    const tinygltf::Accessor* accessor = getAccessor("TEXCOORD_0");
    if (accessor == nullptr) {
        m_texcoordData = std::vector<vec2>();
        return m_texcoordData.value();
    }

    ARKOSE_ASSERT(accessor->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
    ARKOSE_ASSERT(accessor->type == TINYGLTF_TYPE_VEC2);

    const tinygltf::BufferView& view = m_model->bufferViews[accessor->bufferView];
    ARKOSE_ASSERT(view.byteStride == 0 || view.byteStride == 8); // (i.e. tightly packed)

    const tinygltf::Buffer& buffer = m_model->buffers[view.buffer];

    const unsigned char* start = buffer.data.data() + view.byteOffset + accessor->byteOffset;
    auto* first = reinterpret_cast<const vec2*>(start);

    m_texcoordData = std::vector<vec2>(first, first + accessor->count);
    return m_texcoordData.value();
}

const std::vector<vec3>& GltfMesh::normalData() const
{
    SCOPED_PROFILE_ZONE();

    if (m_normalData.has_value())
        return m_normalData.value();

    const tinygltf::Accessor* accessor = getAccessor("NORMAL");
    if (accessor == nullptr) {
        m_normalData = std::vector<vec3>();
        return m_normalData.value();
    }

    ARKOSE_ASSERT(accessor->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
    ARKOSE_ASSERT(accessor->type == TINYGLTF_TYPE_VEC3);

    const tinygltf::BufferView& view = m_model->bufferViews[accessor->bufferView];
    ARKOSE_ASSERT(view.byteStride == 0 || view.byteStride == 12); // (i.e. tightly packed)

    const tinygltf::Buffer& buffer = m_model->buffers[view.buffer];

    const unsigned char* start = buffer.data.data() + view.byteOffset + accessor->byteOffset;
    auto* first = reinterpret_cast<const vec3*>(start);

    m_normalData = std::vector<vec3>(first, first + accessor->count);
    return m_normalData.value();
    ;
}

const std::vector<vec4>& GltfMesh::tangentData() const
{
    SCOPED_PROFILE_ZONE();

    if (m_tangentData.has_value())
        return m_tangentData.value();

    const tinygltf::Accessor* accessor = getAccessor("TANGENT");
    if (accessor == nullptr) {
        m_tangentData = std::vector<vec4>();
        return m_tangentData.value();
    }

    ARKOSE_ASSERT(accessor->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
    ARKOSE_ASSERT(accessor->type == TINYGLTF_TYPE_VEC4);

    const tinygltf::BufferView& view = m_model->bufferViews[accessor->bufferView];
    ARKOSE_ASSERT(view.byteStride == 0 || view.byteStride == 16); // (i.e. tightly packed)

    const tinygltf::Buffer& buffer = m_model->buffers[view.buffer];

    const unsigned char* start = buffer.data.data() + view.byteOffset + accessor->byteOffset;
    auto* first = reinterpret_cast<const vec4*>(start);

    m_tangentData = std::vector<vec4>(first, first + accessor->count);
    return m_tangentData.value();
}

const std::vector<uint32_t>& GltfMesh::indexData() const
{
    SCOPED_PROFILE_ZONE();

    if (m_indexData.has_value())
        return m_indexData.value();

    ARKOSE_ASSERT(isIndexed());
    const tinygltf::Accessor& accessor = m_model->accessors[m_primitive->indices];
    ARKOSE_ASSERT(accessor.type == TINYGLTF_TYPE_SCALAR);

    const tinygltf::BufferView& view = m_model->bufferViews[accessor.bufferView];
    ARKOSE_ASSERT(view.byteStride == 0); // (i.e. tightly packed)

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
    ARKOSE_ASSERT(isIndexed());
    const tinygltf::Accessor& accessor = m_model->accessors[m_primitive->indices];
    ARKOSE_ASSERT(accessor.type == TINYGLTF_TYPE_SCALAR);

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
