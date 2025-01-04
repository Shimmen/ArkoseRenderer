#include "GltfLoader.h"

#include "ark/aabb.h"
#include "core/Assert.h"
#include "core/Logging.h"
#include "core/parallel/ParallelFor.h"
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

    // Make best guesses for images' types
    std::unordered_map<int, ImageType> imageTypeBestGuess {};
    for (tinygltf::Material const& gltfMaterial : gltfModel.materials) {
        // Note that we're relying on all -1 i.e. invalid textures mapping to the same slot which will be unused anyway
        imageTypeBestGuess[gltfMaterial.pbrMetallicRoughness.baseColorTexture.index] = ImageType::sRGBColor;
        imageTypeBestGuess[gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index] = ImageType::GenericData;
        imageTypeBestGuess[gltfMaterial.emissiveTexture.index] = ImageType::sRGBColor;
        imageTypeBestGuess[gltfMaterial.normalTexture.index] = ImageType::NormalMap;
    }

    // Create all images defined in the glTF file (even potentially unused ones)
    size_t textureCount = gltfModel.textures.size();
    result.images.resize(textureCount);
    ParallelFor(textureCount, [&](size_t idx) {

        tinygltf::Texture& gltfTexture = gltfModel.textures[idx];
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
            image->name = gltfImage.name;

        }

        // Assign the best-guess type of this image
        auto entry = imageTypeBestGuess.find(static_cast<int>(idx));
        if (entry != imageTypeBestGuess.end()) {
            if (image) {
                image->setType(entry->second);
            }
        }

        result.images[idx] = std::move(image);
    });

    // Create all materials defined in the glTF file (even potentially unused ones)
    for (size_t idx = 0; idx < gltfModel.materials.size(); ++idx) {
        tinygltf::Material const& gltfMaterial = gltfModel.materials[idx];
        if (std::unique_ptr<MaterialAsset> material = createMaterial(gltfModel, gltfMaterial)) {
            result.materials.push_back(std::move(material));
        }
    }

    // Create all meshes definined in the glTF file (even potentially unused ones)
    for (size_t idx = 0; idx < gltfModel.meshes.size(); ++idx) {
        tinygltf::Mesh const& gltfMesh = gltfModel.meshes[idx];
        if (std::unique_ptr<MeshAsset> staticMesh = createMesh(gltfModel, gltfMesh)) {
            result.meshes.push_back(std::move(staticMesh));
        }
    }

    // Create all skeletons definined in the glTF file (even potentially unused ones)
    for (size_t idx = 0; idx < gltfModel.skins.size(); ++idx) {
        tinygltf::Skin const& gltfSkin = gltfModel.skins[idx];
        if (std::unique_ptr<SkeletonAsset> skeleton = createSkeleton(gltfModel, gltfSkin)) {
            result.skeletons.push_back(std::move(skeleton));
        }
    }

    // Create all animations definined in the glTF file (even potentially unused ones)
    for (size_t idx = 0; idx < gltfModel.animations.size(); ++idx) {
        tinygltf::Animation const& gltfAnimation = gltfModel.animations[idx];
        if (std::unique_ptr<AnimationAsset> animation = createAnimation(gltfModel, gltfAnimation)) {
            result.animations.push_back(std::move(animation));
        }
    }

    constexpr int TransformStackDepth = 16;
    std::vector<Transform> transformStack {};
    transformStack.reserve(TransformStackDepth);

    std::function<void(const tinygltf::Node&, Transform*)> createObjectsRecursively = [&](const tinygltf::Node& node, Transform* parent) {

        // If this triggers we need to keep a larger stack of transforms
        ARKOSE_ASSERT(transformStack.size() < TransformStackDepth);

        Transform& transform = transformStack.emplace_back(parent);
        createTransformForNode(transform, node);

        if (node.mesh != -1) {

            // TODO: Maybe allow exporting Transforms as is, without flattening first?
            Transform flattenedTransform = transform.flattened();

            MeshAsset* mesh = result.meshes[node.mesh].get();
            result.meshInstances.push_back({ .mesh = mesh,
                                             .transform = flattenedTransform });
        }

        if (node.camera != -1) {
            tinygltf::Camera const& gltfCamera = gltfModel.cameras[node.camera];

            ImportedCamera& camera = result.cameras.emplace_back();
            camera.name = gltfCamera.name;
            camera.transform = transform;

            if (gltfCamera.type == "perspective") {
                tinygltf::PerspectiveCamera const& gltfPerspectiveCamera = gltfCamera.perspective;
                camera.verticalFieldOfView = static_cast<float>(gltfPerspectiveCamera.yfov);
                camera.zNear = static_cast<float>(gltfPerspectiveCamera.znear);
                camera.zFar = static_cast<float>(gltfPerspectiveCamera.zfar);
            }
        }

        for (int childNodeIdx : node.children) {
            const tinygltf::Node& childNode = gltfModel.nodes[childNodeIdx];
            createObjectsRecursively(childNode, &transform);
        }

        transformStack.pop_back();
    };

    const tinygltf::Scene& gltfScene = gltfModel.scenes[gltfModel.defaultScene];
    for (int nodeIdx : gltfScene.nodes) {
        const tinygltf::Node& node = gltfModel.nodes[nodeIdx];
        createObjectsRecursively(node, nullptr);
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

std::unique_ptr<MeshAsset> GltfLoader::createMesh(const tinygltf::Model& gltfModel, const tinygltf::Mesh& gltfMesh)
{
    SCOPED_PROFILE_ZONE();

    auto mesh = std::make_unique<MeshAsset>();
    mesh->name = gltfMesh.name;

    // Only a sinle LOD used for glTF (without extensions)
    MeshLODAsset& lod0 = mesh->LODs.emplace_back();
    mesh->minLOD = 0;
    mesh->maxLOD = 0;

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

        mesh->boundingBox.expandWithPoint(createVec3(positionAccessor.minValues));
        mesh->boundingBox.expandWithPoint(createVec3(positionAccessor.maxValues));

        MeshSegmentAsset& meshSegment = lod0.meshSegments.emplace_back();

        // Write glTF material index to user data until we can resolve file paths
        const int materialIdx = gltfPrimitive.material;
        meshSegment.userData = materialIdx;

        {
            SCOPED_PROFILE_ZONE_NAMED("Copy position data");

            meshSegment.positions.reserve(positionAccessor.count);
            const vec3* firstPosition = getTypedMemoryBufferForAccessor<vec3>(gltfModel, positionAccessor);
            meshSegment.positions = std::vector<vec3>(firstPosition, firstPosition + positionAccessor.count);
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
            meshSegment.normals = std::vector<vec3>(firstNormal, firstNormal + accessor->count);
        }

        if (const tinygltf::Accessor* accessor = findAccessorForPrimitive(gltfModel, gltfPrimitive, "TANGENT")) {
            SCOPED_PROFILE_ZONE_NAMED("Copy tangent data");

            ARKOSE_ASSERT(accessor->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
            ARKOSE_ASSERT(accessor->type == TINYGLTF_TYPE_VEC4);

            meshSegment.tangents.reserve(accessor->count);
            const vec4* firstTangent = getTypedMemoryBufferForAccessor<vec4>(gltfModel, *accessor);
            meshSegment.tangents = std::vector<vec4>(firstTangent, firstTangent + accessor->count);
        }

        if (const tinygltf::Accessor* accessor = findAccessorForPrimitive(gltfModel, gltfPrimitive, "JOINTS_0")) {
            SCOPED_PROFILE_ZONE_NAMED("Copy joint indices data");

            ARKOSE_ASSERT(accessor->type == TINYGLTF_TYPE_VEC4);

            meshSegment.jointIndices.reserve(accessor->count);

            switch (accessor->componentType) {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
                const ark::tvec4<u8>* firstJointIndices = getTypedMemoryBufferForAccessor<ark::tvec4<u8>>(gltfModel, *accessor);
                for (size_t i = 0; i < accessor->count; ++i) {
                    ark::tvec4<u8> const& valuesAsU8 = *(firstJointIndices + i);
                    meshSegment.jointIndices.emplace_back(static_cast<u16>(valuesAsU8.x),
                                                          static_cast<u16>(valuesAsU8.y),
                                                          static_cast<u16>(valuesAsU8.z),
                                                          static_cast<u16>(valuesAsU8.w));
                }
            } break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
                const ark::tvec4<u16>* firstJointIndices = getTypedMemoryBufferForAccessor<ark::tvec4<u16>>(gltfModel, *accessor);
                meshSegment.jointIndices = std::vector<ark::tvec4<u16>>(firstJointIndices, firstJointIndices + accessor->count);
            } break;
            default:
                NOT_YET_IMPLEMENTED();
            }
        }

        if (const tinygltf::Accessor* accessor = findAccessorForPrimitive(gltfModel, gltfPrimitive, "WEIGHTS_0")) {
            SCOPED_PROFILE_ZONE_NAMED("Copy joint weights data");

            ARKOSE_ASSERT(accessor->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
            ARKOSE_ASSERT(accessor->type == TINYGLTF_TYPE_VEC4);

            meshSegment.jointWeights.reserve(accessor->count);
            const vec4* firstJointWeight = getTypedMemoryBufferForAccessor<vec4>(gltfModel, *accessor);
            meshSegment.jointWeights = std::vector<vec4>(firstJointWeight, firstJointWeight + accessor->count);
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

    // Generate bounding sphere from bounding box (not the tighest sphere necessarily but it'll work)
    vec3 center = (mesh->boundingBox.max + mesh->boundingBox.min) / 2.0f;
    float radius = length(mesh->boundingBox.max - mesh->boundingBox.min) / 2.0f;
    mesh->boundingSphere = geometry::Sphere(center, radius);

    return mesh;
}

std::unique_ptr<AnimationAsset> GltfLoader::createAnimation(tinygltf::Model const& gltfModel, tinygltf::Animation const& gltfAnimation)
{
    SCOPED_PROFILE_ZONE();

    std::unordered_map<size_t, size_t> inputTrackIdxLookup {};

    auto animation = std::make_unique<AnimationAsset>();
    animation->name = gltfAnimation.name;

    for (size_t channelIdx = 0; channelIdx < gltfAnimation.channels.size(); ++channelIdx) {
        tinygltf::AnimationChannel const& gltfAnimationChannel = gltfAnimation.channels[channelIdx];
        tinygltf::AnimationSampler const& gltfAnimationSampler = gltfAnimation.samplers[gltfAnimationChannel.sampler];

        // TODO: Handle different types of animations!
        //  - For bones, we need a bone reference (e.g. name, index, some way to identify in the hierarchy)
        //  - For rigidbody animations of meshes and nodes, we might need some other way to do it.
        int targetNodeIdx = gltfAnimationChannel.target_node;
        tinygltf::Node const& targetNode = gltfModel.nodes[targetNodeIdx];
        std::string targetName = targetNode.name;
        if (targetName.empty()) {
            targetName = fmt::format(FMT_STRING("node{:04}"), targetNodeIdx);
        }

        AnimationTargetProperty targetProperty;
        if (gltfAnimationChannel.target_path == "translation") {
            targetProperty = AnimationTargetProperty::Translation;
        } else if (gltfAnimationChannel.target_path == "rotation") {
            targetProperty = AnimationTargetProperty::Rotation;
        } else if (gltfAnimationChannel.target_path == "scale") {
            targetProperty = AnimationTargetProperty::Scale;
        } else if (gltfAnimationChannel.target_path == "weights") {
            NOT_YET_IMPLEMENTED();
        } else {
            ASSERT_NOT_REACHED();
        }

        AnimationInterpolation interpolation;
        if (gltfAnimationSampler.interpolation == "LINEAR") {
            interpolation = AnimationInterpolation::Linear;
        } else if (gltfAnimationSampler.interpolation == "STEP") {
            interpolation = AnimationInterpolation::Step;
        } else if (gltfAnimationSampler.interpolation == "CUBICSPLINE") {
            interpolation = AnimationInterpolation::CubicSpline;
        } else {
            // glTF allows user specified interpolation.. treat it all like linear
            interpolation = AnimationInterpolation::Linear;
        }

        tinygltf::Accessor const& inputAccessor = gltfModel.accessors[gltfAnimationSampler.input];
        tinygltf::Accessor const& outputAccessor = gltfModel.accessors[gltfAnimationSampler.output];

        // Time (input)
        auto timeEntry = inputTrackIdxLookup.find(gltfAnimationSampler.input);
        if (timeEntry == inputTrackIdxLookup.end()) {
            inputTrackIdxLookup[gltfAnimationSampler.input] = animation->inputTracks.size();
            float const* firstInputValue = getTypedMemoryBufferForAccessor<float>(gltfModel, inputAccessor);
            animation->inputTracks.emplace_back(std::vector<float>(firstInputValue, firstInputValue + inputAccessor.count));
        }

        auto createAnimationChannelAsset = [&]<typename T>(T proxyValue) -> AnimationChannelAsset<T> {
            // Unused, just there to help the compiler to figure out what T is
            (void)proxyValue;

            AnimationChannelAsset<T> channelAsset {};
            channelAsset.targetProperty = targetProperty;
            channelAsset.targetReference = targetName;

            channelAsset.sampler.inputTrackIdx = narrow_cast<u32>(inputTrackIdxLookup[gltfAnimationSampler.input]);
            channelAsset.sampler.interpolation = interpolation;

            // Animated value (output)
            ARKOSE_ASSERT(outputAccessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT); // TODO: Probably will need to relax this
            T const* firstValue = getTypedMemoryBufferForAccessor<T>(gltfModel, outputAccessor);
            channelAsset.sampler.outputValues = std::vector<T>(firstValue, firstValue + outputAccessor.count);

            return channelAsset;
        };

        switch (outputAccessor.type) {
        case TINYGLTF_TYPE_SCALAR:
            animation->floatPropertyChannels.emplace_back(createAnimationChannelAsset(float()));
            break;
        case TINYGLTF_TYPE_VEC2:
            animation->float2PropertyChannels.emplace_back(createAnimationChannelAsset(vec2()));
            break;
        case TINYGLTF_TYPE_VEC3:
            animation->float3PropertyChannels.emplace_back(createAnimationChannelAsset(vec3()));
            break;
        case TINYGLTF_TYPE_VEC4:
            animation->float4PropertyChannels.emplace_back(createAnimationChannelAsset(vec4()));
            break;
        default:
            NOT_YET_IMPLEMENTED();
        }
    }

    return animation;
}

std::unique_ptr<SkeletonAsset> GltfLoader::createSkeleton(tinygltf::Model const& gltfModel, tinygltf::Skin const& gltfSkin)
{
    SCOPED_PROFILE_ZONE();

    // NOTE: Here I'm mapping to a skeleton from a skin. In this context we can think of a skeleton being a general form
    // of a skin, that can potentially be applied to any compatible skinned mesh. The way a skin is represented in glTF
    // it's pretty much just a skeleton anyway, as the skin-parts (joint idx & weights) is part of the vertex data anyway.

    auto skeleton = std::make_unique<SkeletonAsset>();
    skeleton->name = gltfSkin.name;

    tinygltf::Accessor const& invBindMatricesAccessor = gltfModel.accessors[gltfSkin.inverseBindMatrices];
    ARKOSE_ASSERT(invBindMatricesAccessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
    ARKOSE_ASSERT(invBindMatricesAccessor.type == TINYGLTF_TYPE_MAT4);
    [[maybe_unused]] mat4 const* firstInvBindMatrix = getTypedMemoryBufferForAccessor<mat4>(gltfModel, invBindMatricesAccessor);

    std::unordered_map<int, int> jointIdxLookup {};
    for (size_t idx = 0; idx < gltfSkin.joints.size(); ++idx) {
        int nodeIdx = gltfSkin.joints[idx];
        jointIdxLookup[nodeIdx] = narrow_cast<int>(idx);
    }

    // This max is not immediately obvious when in an hierarchy as it is in the asset...
    skeleton->maxJointIdx = narrow_cast<u32>(gltfSkin.joints.size() - 1);

    // Traverse the children of the skeleton root node and keep track of the hierarchy, but only for nodes in gltfSkin.joints
    // as there may be non-joint nodes part of the same hierarchy (may not be true but should work as an assumption I think).

    std::function<void(int nodeIdx, SkeletonJointAsset&, SkeletonJointAsset*)> createSkeletonRecursively =
        [&](int nodeIdx, SkeletonJointAsset& joint, SkeletonJointAsset* parentJoint) {
            tinygltf::Node const& node = gltfModel.nodes[nodeIdx];

            joint.name = node.name;
            joint.index = jointIdxLookup[nodeIdx];

            createTransformForNode(joint.transform, node);
            if (parentJoint != nullptr) {
                joint.transform.setParent(&parentJoint->transform);
            }

            // NOTE: The glTF-supplied inverse bind matrices may include some of the pre-skeleton-root transform things,
            // which we don't care about, as we consider that to be the instance's transform in the scene instead. Therefore
            // we can get the same matrix by just taking the inverse of the bind pose we've built up here.
            //joint.invBindMatrix = *(firstInvBindMatrix + joint.index);
            joint.invBindMatrix = ark::inverse(joint.transform.worldMatrix());

            // NOTE: This ensures we can safely pass around addresses of joints
            joint.children.reserve(node.children.size());

            for (int childNodeIdx : node.children) {
                auto entry = jointIdxLookup.find(childNodeIdx);
                if (entry != jointIdxLookup.end()) {
                    SkeletonJointAsset& childJoint = joint.children.emplace_back();
                    createSkeletonRecursively(childNodeIdx, childJoint, &joint);
                }
            }
        };

    // TODO: Do we need to start at the root of the node tree to resolve the transforms correctly?
    // Now we start at the root joint node, but not the absolute bottom of the node tree.
    createSkeletonRecursively(gltfSkin.skeleton, skeleton->rootJoint, nullptr);

    return skeleton;
}

const tinygltf::Accessor* GltfLoader::findAccessorForPrimitive(const tinygltf::Model& gltfModel, const tinygltf::Primitive& gltfPrimitive, const char* name) const
{
    auto entry = gltfPrimitive.attributes.find(name);
    if (entry == gltfPrimitive.attributes.end()) {
        //ARKOSE_LOG(Error, "glTF loader: primitive is missing attribute of name '{}'", name);
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

        return input;
    };

    auto material = std::make_unique<MaterialAsset>();
    material->name = gltfMaterial.name;

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

    material->doubleSided = gltfMaterial.doubleSided;

    material->metallicFactor = static_cast<float>(gltfMaterial.pbrMetallicRoughness.metallicFactor);
    material->roughnessFactor = static_cast<float>(gltfMaterial.pbrMetallicRoughness.roughnessFactor);

    std::vector<double> e = gltfMaterial.emissiveFactor;
    material->emissiveFactor = vec3((float)e[0], (float)e[1], (float)e[2]);

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
