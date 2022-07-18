#include "GltfLoader.h"

#include "core/Assert.h"
#include "core/Logging.h"
#include "utility/Profiling.h"
#include "utility/FileIO.h"

GltfLoader::LoadResult GltfLoader::load(const std::string& gltfFilePath, LoadMode loadMode)
{
    SCOPED_PROFILE_ZONE();

    LoadResult result {};

    if (!FileIO::isFileReadable(gltfFilePath)) {
        ARKOSE_LOG(Error, "Could not find glTF file at path '{}'", gltfFilePath);
        return result;
    }

    tinygltf::TinyGLTF loader {};
    tinygltf::Model gltfModel {};

    std::string error;
    std::string warning;

    bool loadSuccess = false;
    {
        SCOPED_PROFILE_ZONE_NAMED("TinyGLTF work");
        if (gltfFilePath.ends_with(".gltf")) {
            loadSuccess = loader.LoadASCIIFromFile(&gltfModel, &error, &warning, gltfFilePath);
        } else if (gltfFilePath.ends_with(".glb")) {
            loadSuccess = loader.LoadBinaryFromFile(&gltfModel, &error, &warning, gltfFilePath);
        } else {
            ARKOSE_LOG(Error, "glTF loader: invalid file glTF file path/extension '{}'", gltfFilePath);
            return result;
        }
    }

    if (!loadSuccess) {
        ARKOSE_LOG(Error, "glTF loader: could not load file '{}'", gltfFilePath);
        return result;
    }

    if (!error.empty()) {
        ARKOSE_LOG(Error, "glTF loader: {}", error);
        return result;
    }

    if (!warning.empty()) {
        ARKOSE_LOG(Warning, "glTF loader: {}", warning);
    }

    if (gltfModel.defaultScene == -1 && gltfModel.scenes.size() > 1) {
        ARKOSE_LOG(Warning, "glTF loader: more than one scene defined in glTF file '{}' but no default scene. Will pick scene 0.", gltfFilePath);
        gltfModel.defaultScene = 0;
    }

    constexpr int TransformStackDepth = 16;
    std::vector<Transform> transformStack {};
    transformStack.reserve(TransformStackDepth);

    std::function<void(const tinygltf::Node&, Transform*)> createMeshesRecursively = [&](const tinygltf::Node& node, Transform* parent) {

        // If this triggers we need to keep a larger stack of transforms
        ARKOSE_ASSERT(transformStack.size() < TransformStackDepth);

        Transform& transform = transformStack.emplace_back(parent);
        createTransformForNode(transform, node);

        if (node.mesh != -1) {
            const tinygltf::Mesh& gltfMesh = gltfModel.meshes[node.mesh];

            // TODO: Ensure we're in fact loading in a *static* mesh
            if (std::unique_ptr<StaticMesh> staticMesh = createStaticMesh(gltfModel, gltfMesh, transform)) {
                result.staticMeshes.push_back(std::move(staticMesh));
            }
        }

        for (int childNodeIdx : node.children) {
            const tinygltf::Node& childNode = gltfModel.nodes[childNodeIdx];
            createMeshesRecursively(childNode, &transform);
        }
    };

    const tinygltf::Scene& gltfScene = gltfModel.scenes[gltfModel.defaultScene];
    for (int nodeIdx : gltfScene.nodes) {
        const tinygltf::Node& node = gltfModel.nodes[nodeIdx];
        createMeshesRecursively(node, nullptr);
    }

    // Create all materials defined in the glTF file (even if some may be unused)
    for (const tinygltf::Material& gltfMaterial : gltfModel.materials) {
        std::unique_ptr<Material> material = createMaterial(gltfModel, gltfMaterial, findDirectoryOfGltfFile(gltfFilePath));
        result.materials.push_back(std::move(material));
    }

    return result;
}

std::string GltfLoader::findDirectoryOfGltfFile(const std::string& gltfFilePath) const
{
    size_t lastSlash = gltfFilePath.rfind('/');
    if (lastSlash == std::string::npos) {
        lastSlash = gltfFilePath.rfind('\\');
        if (lastSlash == std::string::npos) {
            return "";
        }
    }

    auto directory = gltfFilePath.substr(0, lastSlash + 1);

    return directory;
}

void GltfLoader::createTransformForNode(Transform& transform, const tinygltf::Node& node) const
{
    if (!node.matrix.empty()) {
        const auto& vals = node.matrix;
        ARKOSE_ASSERT(vals.size() == 16);
        mat4 matrix = mat4(vec4((float)vals[0], (float)vals[1], (float)vals[2], (float)vals[3]),
                           vec4((float)vals[4], (float)vals[5], (float)vals[6], (float)vals[7]),
                           vec4((float)vals[8], (float)vals[9], (float)vals[10], (float)vals[11]),
                           vec4((float)vals[12], (float)vals[13], (float)vals[14], (float)vals[15]));
        transform.setFromMatrix(matrix);
    } else {

        if (node.translation.size() == 3) {
            vec3 translation = createVec3(node.translation);
            transform.setTranslation(translation);
        }

        if (node.rotation.size() == 4) {
            quat orientaiton = quat(createVec3(node.rotation),
                                    static_cast<float>(node.rotation[3]));
            transform.setOrientation(orientaiton);
        }

        if (node.scale.size() == 3) {
            vec3 scale = createVec3(node.scale);
            transform.setScale(scale);
        }
    }
}

std::unique_ptr<StaticMesh> GltfLoader::createStaticMesh(const tinygltf::Model& gltfModel, const tinygltf::Mesh& gltfMesh, Transform& transform) const
{
    SCOPED_PROFILE_ZONE();

    // We pre-bake all mesh transforms if there are any. World matrix here essentially just means it contains the whole
    // stack of matrices from the local one all the way up the node stack. We don't have any object-to-world transform.
    mat4 meshMatrix = transform.worldMatrix();
    mat3 meshNormalMatrix = transform.worldNormalMatrix();

    auto staticMesh = std::make_unique<StaticMesh>();
    staticMesh->m_name = gltfMesh.name;

    // Only a sinle LOD used for glTF (without extensions)
    StaticMeshLOD& lod0 = staticMesh->m_lods.emplace_back();

    lod0.m_meshSegments.reserve(gltfMesh.primitives.size());
    for (size_t primIdx = 0; primIdx < gltfMesh.primitives.size(); ++primIdx) {

        SCOPED_PROFILE_ZONE_NAMED("Creating mesh segment");

        const tinygltf::Primitive& gltfPrimitive = gltfMesh.primitives[primIdx];

        if (gltfPrimitive.mode != TINYGLTF_MODE_TRIANGLES) {
            ARKOSE_LOG(Fatal, "glTF loader: only triangle list meshes are supported (for now), skipping primitive.");
            continue;
        }

        const tinygltf::Accessor& positionAccessor = *findAccessorForPrimitive(gltfModel, gltfPrimitive, "POSITION");
        ARKOSE_ASSERT(positionAccessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
        ARKOSE_ASSERT(positionAccessor.type == TINYGLTF_TYPE_VEC3);

        vec3 posMin = meshMatrix * createVec3(positionAccessor.minValues);
        vec3 posMax = meshMatrix * createVec3(positionAccessor.maxValues);
        lod0.m_boundingBox = ark::aabb3(posMin, posMax);

        vec3 center = (posMax + posMin) / 2.0f;
        float radius = length(posMax - posMin) / 2.0f;
        lod0.m_boundingSphere = geometry::Sphere(center, radius);

        StaticMeshSegment& meshSegment = lod0.m_meshSegments.emplace_back();

        // NOTE: Materials here in this stage use handles which are gltf-file-referred, meaning they will need to be translated
        // into whatever the scene works with before they can be used in a scene with multiple loaded models or similar.
        int materialIdx = gltfPrimitive.material;
        meshSegment.m_material = MaterialHandle(materialIdx);

        {
            SCOPED_PROFILE_ZONE_NAMED("Copy position data");

            meshSegment.m_positions.reserve(positionAccessor.count);
            const vec3* firstPosition = getTypedMemoryBufferForAccessor<vec3>(gltfModel, positionAccessor);
            for (size_t i = 0; i < positionAccessor.count; ++i) {
                vec3 sourceValue = *(firstPosition + i);
                vec3 transformedValue = meshMatrix * sourceValue;
                meshSegment.m_positions.push_back(transformedValue);
            }
        }

        if (const tinygltf::Accessor* accessor = findAccessorForPrimitive(gltfModel, gltfPrimitive, "TEXCOORD_0")) {
            SCOPED_PROFILE_ZONE_NAMED("Copy texcoord data");

            ARKOSE_ASSERT(accessor->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
            ARKOSE_ASSERT(accessor->type == TINYGLTF_TYPE_VEC2);

            const vec2* firstTexcoord = getTypedMemoryBufferForAccessor<vec2>(gltfModel, *accessor);
            meshSegment.m_texcoord0s = std::vector<vec2>(firstTexcoord, firstTexcoord + accessor->count);
        }

        if (const tinygltf::Accessor* accessor = findAccessorForPrimitive(gltfModel, gltfPrimitive, "NORMAL")) {
            SCOPED_PROFILE_ZONE_NAMED("Copy normal data");

            ARKOSE_ASSERT(accessor->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
            ARKOSE_ASSERT(accessor->type == TINYGLTF_TYPE_VEC3);

            meshSegment.m_normals.reserve(accessor->count);
            const vec3* firstNormal = getTypedMemoryBufferForAccessor<vec3>(gltfModel, *accessor);
            for (size_t i = 0; i < accessor->count; ++i) {
                vec3 sourceNormal = *(firstNormal + i);
                vec3 transformedNormal = meshNormalMatrix * sourceNormal;
                meshSegment.m_normals.push_back(transformedNormal);
            }
        }

        if (const tinygltf::Accessor* accessor = findAccessorForPrimitive(gltfModel, gltfPrimitive, "TANGENT")) {
            SCOPED_PROFILE_ZONE_NAMED("Copy tangent data");

            ARKOSE_ASSERT(accessor->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
            ARKOSE_ASSERT(accessor->type == TINYGLTF_TYPE_VEC4);

            meshSegment.m_tangents.reserve(accessor->count);
            const vec4* firstTangent = getTypedMemoryBufferForAccessor<vec4>(gltfModel, *accessor);
            for (size_t i = 0; i < accessor->count; ++i) {
                vec4 sourceTangent = *(firstTangent + i);
                vec3 transformedTangent = meshNormalMatrix * sourceTangent.xyz();
                vec4 finalTangent = vec4(transformedTangent, sourceTangent.w);
                meshSegment.m_tangents.push_back(finalTangent);
            }
        }

        if (gltfPrimitive.indices != -1) {

            const tinygltf::Accessor& accessor = gltfModel.accessors[gltfPrimitive.indices];
            ARKOSE_ASSERT(accessor.type == TINYGLTF_TYPE_SCALAR);

            meshSegment.m_indices.reserve(accessor.count);

            switch (accessor.componentType) {
            case TINYGLTF_COMPONENT_TYPE_BYTE:
                copyIndexData<int8_t>(meshSegment.m_indices, gltfModel, accessor);
                break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                copyIndexData<uint8_t>(meshSegment.m_indices, gltfModel, accessor);
                break;
            case TINYGLTF_COMPONENT_TYPE_SHORT:
                copyIndexData<int16_t>(meshSegment.m_indices, gltfModel, accessor);
                break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                copyIndexData<uint16_t>(meshSegment.m_indices, gltfModel, accessor);
                break;
            case TINYGLTF_COMPONENT_TYPE_INT:
                copyIndexData<int32_t>(meshSegment.m_indices, gltfModel, accessor);
                break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                copyIndexData<uint32_t>(meshSegment.m_indices, gltfModel, accessor);
                break;
            default:
                ASSERT_NOT_REACHED();
            }
        }
    }

    return staticMesh;
}

const tinygltf::Accessor* GltfLoader::findAccessorForPrimitive(const tinygltf::Model& gltfModel, const tinygltf::Primitive& gltfPrimitive, const char* name) const
{
    auto entry = gltfPrimitive.attributes.find(name);
    if (entry == gltfPrimitive.attributes.end()) {
        ARKOSE_LOG(Error, "glTF loader: primitive is missing attribute of name '{}'", name);
        return nullptr;
    }

    return &gltfModel.accessors[entry->second];
}

std::unique_ptr<Material> GltfLoader::createMaterial(const tinygltf::Model& gltfModel, const tinygltf::Material& gltfMaterial, const std::string& gltfFileDirectory) const
{
    SCOPED_PROFILE_ZONE();

    auto toTextureDesc = [&](int texIndex, bool sRGB, vec4 fallbackColor) -> Material::TextureDescription {
        if (texIndex == -1) {
            Material::TextureDescription desc {};
            desc.fallbackColor = fallbackColor;
            desc.sRGB = sRGB;
            return desc;
        }

        auto& texture = gltfModel.textures[texIndex];
        auto& sampler = gltfModel.samplers[texture.sampler];
        auto& image = gltfModel.images[texture.source];

        Material::TextureDescription desc;
        if (!image.uri.empty()) {
            desc = Material::TextureDescription(gltfFileDirectory + image.uri);
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

            const tinygltf::BufferView& bufferView = gltfModel.bufferViews[image.bufferView];
            const tinygltf::Buffer& buffer = gltfModel.buffers[bufferView.buffer];

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

vec3 GltfLoader::createVec3(const std::vector<double>& values) const
{
    ARKOSE_ASSERT(values.size() >= 3);
    return { static_cast<float>(values[0]),
             static_cast<float>(values[1]),
             static_cast<float>(values[2]) };
}
