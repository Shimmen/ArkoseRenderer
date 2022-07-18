#pragma once

#include "rendering/StaticMesh.h"
#include "scene/Transform.h"
#include <string>
#include <memory>
#include <tiny_gltf.h>

class GltfLoader {

public:

    GltfLoader() = default;
    ~GltfLoader() = default;

    enum class LoadMode {
        // Load the meshes with materials from the glTF file, but ignore camera & lights etc.
        Meshes,
        // Load in the glTF file as is, including cameras & lights
        //Scene,
    };

    struct LoadResult {
        std::vector<std::unique_ptr<Material>> materials {};
        std::vector<std::unique_ptr<StaticMesh>> staticMeshes {};
    };

    // NOTE: This will always try to load and never cache. If you want to cache, do that before calling this
    LoadResult load(const std::string& gltfFilePath, LoadMode);

private:

    std::unique_ptr<Material> createMaterial(const tinygltf::Model&, const tinygltf::Material&, const std::string& gltfFilePath) const;
    std::unique_ptr<StaticMesh> createStaticMesh(const tinygltf::Model&, const tinygltf::Mesh&, Transform&) const;

    std::string findDirectoryOfGltfFile(const std::string& gltfFileDirectory) const;

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
