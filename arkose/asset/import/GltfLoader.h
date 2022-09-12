#pragma once

#include "core/Assert.h"
#include "asset/AssetImporter.h"
#include "asset/ImageAsset.h"
#include "asset/MaterialAsset.h"
#include "asset/StaticMeshAsset.h"
#include "scene/Transform.h"
#include <string>
#include <memory>
#include <tiny_gltf.h>

class GltfLoader_NEW {

public:

    GltfLoader_NEW() = default;
    ~GltfLoader_NEW() = default;

    ImportResult load(const std::string& gltfFilePath);

private:

    std::string m_gltfFilePath {};

    std::unique_ptr<MaterialAsset> createMaterial(const tinygltf::Model&, const tinygltf::Material&);
    std::unique_ptr<StaticMeshAsset> createStaticMesh(const tinygltf::Model&, const tinygltf::Mesh&, Transform&);

    vec3 createVec3(const std::vector<double>&) const;
    void createTransformForNode(Transform&, const tinygltf::Node&) const;

    Arkose::Asset::Vec3 createAssetVec3(vec3) const;
    Arkose::Asset::Vec4 createAssetVec4(vec4) const;

    const tinygltf::Accessor* findAccessorForPrimitive(const tinygltf::Model&, const tinygltf::Primitive&, const char* name) const;

    template<typename Type>
    const Type* getTypedMemoryBufferForAccessor(const tinygltf::Model&, const tinygltf::Accessor&) const;

    template<typename SourceType>
    void copyIndexData(std::vector<uint32_t>& target, const tinygltf::Model&, const tinygltf::Accessor&) const;
};

template<typename Type>
const Type* GltfLoader_NEW::getTypedMemoryBufferForAccessor(const tinygltf::Model& gltfModel, const tinygltf::Accessor& gltfAccessor) const
{
    const tinygltf::BufferView& gltfView = gltfModel.bufferViews[gltfAccessor.bufferView];
    ARKOSE_ASSERT(gltfView.byteStride == 0 || gltfView.byteStride == sizeof(Type)); // (i.e. tightly packed)

    const tinygltf::Buffer& gltfBuffer = gltfModel.buffers[gltfView.buffer];

    const unsigned char* start = gltfBuffer.data.data() + gltfView.byteOffset + gltfAccessor.byteOffset;
    auto* firstElement = reinterpret_cast<const Type*>(start);

    return firstElement;
}

template<typename SourceType>
void GltfLoader_NEW::copyIndexData(std::vector<uint32_t>& target, const tinygltf::Model& gltfModel, const tinygltf::Accessor& gltfAccessor) const
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
