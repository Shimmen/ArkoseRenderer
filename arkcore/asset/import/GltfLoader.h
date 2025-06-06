#pragma once

#include "core/Assert.h"
#include "asset/AnimationAsset.h"
#include "asset/ImageAsset.h"
#include "asset/MaterialAsset.h"
#include "asset/MeshAsset.h"
#include "asset/import/AssetImporter.h"
#include "scene/Transform.h"
#include <string>
#include <memory>
#include <tiny_gltf.h>

class GltfLoader {

public:

    GltfLoader() = default;
    ~GltfLoader() = default;

    ImportResult load(std::filesystem::path const& gltfFilePath);

private:

    std::filesystem::path m_gltfFilePath {};

    std::unique_ptr<MaterialAsset> createMaterial(tinygltf::Model const&, tinygltf::Material const&);
    std::unique_ptr<MeshAsset> createMesh(tinygltf::Model const&, tinygltf::Mesh const&);
    std::unique_ptr<AnimationAsset> createAnimation(tinygltf::Model const&, tinygltf::Animation const&);
    std::unique_ptr<SkeletonAsset> createSkeleton(tinygltf::Model const&, tinygltf::Skin const&);
    std::unique_ptr<LightAsset> createLight(tinygltf::Model const&, tinygltf::Value const&);

    vec3 createVec3(const std::vector<double>&) const;
    void createTransformForNode(Transform&, const tinygltf::Node&) const;

    const tinygltf::Accessor* findAccessorForPrimitive(const tinygltf::Model&, const tinygltf::Primitive&, const char* name) const;

    template<typename Type>
    const Type* getTypedMemoryBufferForAccessor(const tinygltf::Model&, const tinygltf::Accessor&) const;

    template<typename SourceType>
    void copyIndexData(std::vector<uint32_t>& target, const tinygltf::Model&, const tinygltf::Accessor&) const;
};

template<typename Type>
const Type* GltfLoader::getTypedMemoryBufferForAccessor(const tinygltf::Model& gltfModel, const tinygltf::Accessor& gltfAccessor) const
{
    const tinygltf::BufferView& gltfView = gltfModel.bufferViews[gltfAccessor.bufferView];
    ARKOSE_ASSERT(gltfView.byteStride == 0 || gltfView.byteStride == sizeof(Type)); // (i.e. tightly packed)

    const tinygltf::Buffer& gltfBuffer = gltfModel.buffers[gltfView.buffer];

    const unsigned char* start = gltfBuffer.data.data() + gltfView.byteOffset + gltfAccessor.byteOffset;
    auto* firstElement = reinterpret_cast<const Type*>(start);

    return firstElement;
}

template<typename SourceType>
void GltfLoader::copyIndexData(std::vector<uint32_t>& target, const tinygltf::Model& gltfModel, const tinygltf::Accessor& gltfAccessor) const
{
    SCOPED_PROFILE_ZONE_NAMED("Copy index data");

    const tinygltf::BufferView& gltfView = gltfModel.bufferViews[gltfAccessor.bufferView];
    ARKOSE_ASSERT(gltfView.byteStride == 0); // (i.e. tightly packed)

    const tinygltf::Buffer& buffer = gltfModel.buffers[gltfView.buffer];
    const unsigned char* start = buffer.data.data() + gltfView.byteOffset + gltfAccessor.byteOffset;

    auto* firstSourceValue = reinterpret_cast<const SourceType*>(start);

    for (size_t i = 0; i < gltfAccessor.count; ++i) {
        const SourceType* sourceValue = firstSourceValue + i;
        uint32_t targetValue = static_cast<uint32_t>(*sourceValue);
        target.emplace_back(targetValue);
    }
}
