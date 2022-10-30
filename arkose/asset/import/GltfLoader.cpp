#include "GltfLoader.h"

#include "ark/aabb.h"
#include "core/Assert.h"
#include "core/Logging.h"
#include "utility/Profiling.h"
#include "utility/FileIO.h"

ImportResult GltfLoader::load(const std::string& gltfFilePath)
{
    SCOPED_PROFILE_ZONE();

    ImportResult result {};

    if (!FileIO::isFileReadable(gltfFilePath)) {
        ARKOSE_LOG(Error, "Could not find glTF file at path '{}'", gltfFilePath);
        return result;
    }

    m_gltfFilePath = gltfFilePath;

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

    std::string gltfDirectory = std::string(FileIO::extractDirectoryFromPath(gltfFilePath));

    // Make best guesses for images' color spaces
    std::unordered_map<int, ColorSpace> imageColorSpaceBestGuess {};
    for (tinygltf::Material const& gltfMaterial : gltfModel.materials) {
        auto imageIdxForTexture = [&](int gltfTextureIdx) {
            if (gltfTextureIdx == -1) {
                return -1;
            }

            tinygltf::Texture& gltfTexture = gltfModel.textures[gltfTextureIdx];
            return gltfTexture.source;
        };

        // Note that we're relying on all -1 i.e. invalid textures mapping to the same slot which will be unused anyway
        imageColorSpaceBestGuess[imageIdxForTexture(gltfMaterial.pbrMetallicRoughness.baseColorTexture.index)] = ColorSpace::sRGB_encoded;
        imageColorSpaceBestGuess[imageIdxForTexture(gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index)] = ColorSpace::Data;
        imageColorSpaceBestGuess[imageIdxForTexture(gltfMaterial.emissiveTexture.index)] = ColorSpace::sRGB_encoded;
        imageColorSpaceBestGuess[imageIdxForTexture(gltfMaterial.normalTexture.index)] = ColorSpace::Data;
    }

    // Create all images defined in the glTF file (even potentially unused ones)
    for (size_t idx = 0; idx < gltfModel.images.size(); ++idx) {

        tinygltf::Texture& gltfTexture = gltfModel.textures[idx];
        tinygltf::Sampler& gltfSampler = gltfModel.samplers[gltfTexture.sampler];
        tinygltf::Image& gltfImage = gltfModel.images[gltfTexture.source];

        std::unique_ptr<ImageAsset> image {};
        if (not gltfImage.uri.empty()) {

            std::string absolutePath = gltfDirectory + gltfImage.uri;
            std::string path = FileIO::normalizePath(absolutePath);
            image = ImageAsset::createFromSourceAsset(path);

        } else {

            tinygltf::BufferView const& gltfBufferView = gltfModel.bufferViews[gltfImage.bufferView];
            tinygltf::Buffer const& gltfBuffer = gltfModel.buffers[gltfBufferView.buffer];

            size_t encodedDataSize = gltfBufferView.byteLength;
            const uint8_t* encodedData = gltfBuffer.data.data() + gltfBufferView.byteOffset;

            image = ImageAsset::createFromSourceAsset(encodedData, encodedDataSize);

        }

        // Assign the best-guess color space for this image
        auto entry = imageColorSpaceBestGuess.find(static_cast<int>(idx));
        if (entry != imageColorSpaceBestGuess.end()) {
            image->setColorSpace(entry->second);
        }

        // Write glTF image index to user data until we can resolve file paths
        int imageIdx = static_cast<int>(idx);
        image->userData = imageIdx;

        result.images.push_back(std::move(image));
    }

    // Create all materials defined in the glTF file (even potentially unused ones)
    for (size_t idx = 0; idx < gltfModel.materials.size(); ++idx) {
        tinygltf::Material const& gltfMaterial = gltfModel.materials[idx];
        if (std::unique_ptr<MaterialAsset> material = createMaterial(gltfModel, gltfMaterial)) {

            // Write glTF material index to user data until we can resolve file paths
            int materialIdx = static_cast<int>(idx);
            material->userData = materialIdx;

            result.materials.push_back(std::move(material));
        }
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
            if (std::unique_ptr<StaticMeshAsset> staticMesh = createStaticMesh(gltfModel, gltfMesh, transform)) {
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

    return result;
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

std::unique_ptr<StaticMeshAsset> GltfLoader::createStaticMesh(const tinygltf::Model& gltfModel, const tinygltf::Mesh& gltfMesh, Transform& transform)
{
    SCOPED_PROFILE_ZONE();

    // We pre-bake all mesh transforms if there are any. World matrix here essentially just means it contains the whole
    // stack of matrices from the local one all the way up the node stack. We don't have any object-to-world transform.
    mat4 meshMatrix = transform.worldMatrix();
    mat3 meshNormalMatrix = transform.worldNormalMatrix();

    auto staticMesh = std::make_unique<StaticMeshAsset>();
    staticMesh->name = gltfMesh.name;

    // Only a sinle LOD used for glTF (without extensions)
    StaticMeshLODAsset& lod0 = staticMesh->LODs.emplace_back();
    staticMesh->minLOD = 0;
    staticMesh->maxLOD = 0;

    // NOTE: Using reserve is important to maintain the mesh segment addresses for the segment material map!
    lod0.meshSegments.reserve(gltfMesh.primitives.size());

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

        ark::aabb3 localAabb = ark::aabb3(createVec3(positionAccessor.minValues), createVec3(positionAccessor.maxValues));
        ark::aabb3 aabb = localAabb.transformed(meshMatrix);
        staticMesh->boundingBox = ark::aabb3(aabb.min, aabb.max);

        vec3 center = (aabb.max + aabb.min) / 2.0f;
        float radius = length(aabb.max - aabb.min) / 2.0f;
        staticMesh->boundingSphere = geometry::Sphere(center, radius);

        StaticMeshSegmentAsset& meshSegment = lod0.meshSegments.emplace_back();

        // Write glTF material index to user data until we can resolve file paths
        const int materialIdx = gltfPrimitive.material;
        meshSegment.userData = materialIdx;

        {
            SCOPED_PROFILE_ZONE_NAMED("Copy position data");

            meshSegment.positions.reserve(positionAccessor.count);
            const vec3* firstPosition = getTypedMemoryBufferForAccessor<vec3>(gltfModel, positionAccessor);
            for (size_t i = 0; i < positionAccessor.count; ++i) {
                vec3 sourceValue = *(firstPosition + i);
                vec3 transformedValue = meshMatrix * sourceValue;
                meshSegment.positions.push_back(transformedValue);
            }
        }

        if (const tinygltf::Accessor* accessor = findAccessorForPrimitive(gltfModel, gltfPrimitive, "TEXCOORD_0")) {
            SCOPED_PROFILE_ZONE_NAMED("Copy texcoord data");

            ARKOSE_ASSERT(accessor->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
            ARKOSE_ASSERT(accessor->type == TINYGLTF_TYPE_VEC2);

            const vec2* firstTexcoord = getTypedMemoryBufferForAccessor<vec2>(gltfModel, *accessor);
            meshSegment.texcoord0s = std::vector<vec2>(firstTexcoord, firstTexcoord + accessor->count);
        }

        if (const tinygltf::Accessor* accessor = findAccessorForPrimitive(gltfModel, gltfPrimitive, "NORMAL")) {
            SCOPED_PROFILE_ZONE_NAMED("Copy normal data");

            ARKOSE_ASSERT(accessor->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
            ARKOSE_ASSERT(accessor->type == TINYGLTF_TYPE_VEC3);

            meshSegment.normals.reserve(accessor->count);
            const vec3* firstNormal = getTypedMemoryBufferForAccessor<vec3>(gltfModel, *accessor);
            for (size_t i = 0; i < accessor->count; ++i) {
                vec3 sourceNormal = *(firstNormal + i);
                vec3 transformedNormal = meshNormalMatrix * sourceNormal;
                meshSegment.normals.push_back(transformedNormal);
            }
        }

        if (const tinygltf::Accessor* accessor = findAccessorForPrimitive(gltfModel, gltfPrimitive, "TANGENT")) {
            SCOPED_PROFILE_ZONE_NAMED("Copy tangent data");

            ARKOSE_ASSERT(accessor->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
            ARKOSE_ASSERT(accessor->type == TINYGLTF_TYPE_VEC4);

            meshSegment.tangents.reserve(accessor->count);
            const vec4* firstTangent = getTypedMemoryBufferForAccessor<vec4>(gltfModel, *accessor);
            for (size_t i = 0; i < accessor->count; ++i) {
                vec4 sourceTangent = *(firstTangent + i);
                vec3 transformedTangent = meshNormalMatrix * sourceTangent.xyz();
                vec4 finalTangent = vec4(transformedTangent, sourceTangent.w);
                meshSegment.tangents.push_back(finalTangent);
            }
        }

        if (gltfPrimitive.indices != -1) {

            const tinygltf::Accessor& accessor = gltfModel.accessors[gltfPrimitive.indices];
            ARKOSE_ASSERT(accessor.type == TINYGLTF_TYPE_SCALAR);

            meshSegment.indices.reserve(accessor.count);

            switch (accessor.componentType) {
            case TINYGLTF_COMPONENT_TYPE_BYTE:
                copyIndexData<int8_t>(meshSegment.indices, gltfModel, accessor);
                break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                copyIndexData<uint8_t>(meshSegment.indices, gltfModel, accessor);
                break;
            case TINYGLTF_COMPONENT_TYPE_SHORT:
                copyIndexData<int16_t>(meshSegment.indices, gltfModel, accessor);
                break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                copyIndexData<uint16_t>(meshSegment.indices, gltfModel, accessor);
                break;
            case TINYGLTF_COMPONENT_TYPE_INT:
                copyIndexData<int32_t>(meshSegment.indices, gltfModel, accessor);
                break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                copyIndexData<uint32_t>(meshSegment.indices, gltfModel, accessor);
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

std::unique_ptr<MaterialAsset> GltfLoader::createMaterial(const tinygltf::Model& gltfModel, const tinygltf::Material& gltfMaterial)
{
    SCOPED_PROFILE_ZONE();

    auto toMaterialInput = [&](int texIndex) -> std::optional<MaterialInput> {

        if (texIndex == -1) {
            return {};
        }

        auto input = std::make_optional<MaterialInput>();

        auto& gltfTexture = gltfModel.textures[texIndex];
        auto& gltfSampler = gltfModel.samplers[gltfTexture.sampler];

        // Write glTF image index to user data until we can resolve file paths
        input->userData = texIndex;

        auto wrapModeFromTinyGltf = [](int filterMode) -> ImageWrapMode {
            switch (filterMode) {
            case TINYGLTF_TEXTURE_WRAP_REPEAT:
                return ImageWrapMode::Repeat;
            case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:
                return ImageWrapMode::ClampToEdge;
            case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT:
                return ImageWrapMode::MirroredRepeat;
            default:
                ASSERT_NOT_REACHED();
            }
        };

        input->wrapModes = ImageWrapModes(wrapModeFromTinyGltf(gltfSampler.wrapS),
                                                wrapModeFromTinyGltf(gltfSampler.wrapT),
                                                wrapModeFromTinyGltf(gltfSampler.wrapR));

        switch (gltfSampler.minFilter) {
        case TINYGLTF_TEXTURE_FILTER_NEAREST:
            input->minFilter = ImageFilter::Nearest;
            input->useMipmapping = false;
            break;
        case TINYGLTF_TEXTURE_FILTER_LINEAR:
            input->minFilter = ImageFilter::Linear;
            input->useMipmapping = false;
            break;
        case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
            input->minFilter = ImageFilter::Nearest;
            input->mipFilter = ImageFilter::Nearest;
            input->useMipmapping = true;
            break;
        case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
            input->minFilter = ImageFilter::Nearest;
            input->mipFilter = ImageFilter::Linear;
            input->useMipmapping = true;
            break;
        case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
            input->minFilter = ImageFilter::Linear;
            input->mipFilter = ImageFilter::Nearest;
            input->useMipmapping = true;
            break;
        case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:
            input->minFilter = ImageFilter::Linear;
            input->mipFilter = ImageFilter::Linear;
            input->useMipmapping = true;
            break;
        case -1:
            // "glTF 2.0 spec does not define default value for `minFilter` and `magFilter`. Set -1 in TinyGLTF(issue #186)"
            input->minFilter = ImageFilter::Linear;
            input->mipFilter = ImageFilter::Linear;
            input->useMipmapping = true;
            break;
        default:
            ASSERT_NOT_REACHED();
        }

        switch (gltfSampler.magFilter) {
        case TINYGLTF_TEXTURE_FILTER_NEAREST:
            input->magFilter = ImageFilter::Nearest;
            break;
        case TINYGLTF_TEXTURE_FILTER_LINEAR:
            input->magFilter = ImageFilter::Linear;
            break;
        case -1:
            // "glTF 2.0 spec does not define default value for `minFilter` and `magFilter`. Set -1 in TinyGLTF(issue #186)"
            input->magFilter = ImageFilter::Linear;
            break;
        default:
            ASSERT_NOT_REACHED();
        }

        // For now we only support on-line mip map generation
        if (input->useMipmapping) {
            input->generateMipmapsAtRuntime = true;
        }

        return input;
    };

    auto material = std::make_unique<MaterialAsset>();

    if (gltfMaterial.alphaMode == "OPAQUE") {
        material->blendMode = BlendMode::Opaque;
    } else if (gltfMaterial.alphaMode == "BLEND") {
        material->blendMode = BlendMode::Translucent;
    } else if (gltfMaterial.alphaMode == "MASK") {
        material->blendMode = BlendMode::Masked;
        material->maskCutoff = static_cast<float>(gltfMaterial.alphaCutoff);
    } else {
        ASSERT_NOT_REACHED();
    }

    std::vector<double> c = gltfMaterial.pbrMetallicRoughness.baseColorFactor;
    material->colorTint = vec4((float)c[0], (float)c[1], (float)c[2], (float)c[3]);

    int baseColorIdx = gltfMaterial.pbrMetallicRoughness.baseColorTexture.index;
    material->baseColor = toMaterialInput(baseColorIdx);

    int emissiveIdx = gltfMaterial.emissiveTexture.index;
    material->emissiveColor = toMaterialInput(emissiveIdx);

    int normalMapIdx = gltfMaterial.normalTexture.index;
    material->normalMap = toMaterialInput(normalMapIdx);

    int metallicRoughnessIdx = gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index;
    material->materialProperties = toMaterialInput(metallicRoughnessIdx);

    return material;
}

vec3 GltfLoader::createVec3(const std::vector<double>& values) const
{
    ARKOSE_ASSERT(values.size() >= 3);
    return { static_cast<float>(values[0]),
             static_cast<float>(values[1]),
             static_cast<float>(values[2]) };
}
