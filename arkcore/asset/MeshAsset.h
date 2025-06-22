#pragma once

#include "asset/Asset.h"
#include "asset/MaterialAsset.h"
#include "core/Types.h"
#include "core/math/Sphere.h"
#include "physics/PhysicsMesh.h"
#include "scene/Vertex.h"
#include <ark/aabb.h>
#include <string>
#include <string_view>
#include <vector>

struct PhysicsMesh;

struct MeshletAsset {
    u32 firstIndex {};
    u32 triangleCount {};

    u32 firstVertex {};
    u32 vertexCount {};

    vec3 center {};
    float radius {};
};

struct MeshletDataAsset {
    std::vector<MeshletAsset> meshlets {};
    std::vector<u32> meshletVertexIndirection {};
    std::vector<u32> meshletIndices {};
};

struct OpacityMicroMapDataAsset {
    std::vector<std::byte> ommSdkSerializedData;
};

class MeshSegmentAsset {
public:
    MeshSegmentAsset();
    ~MeshSegmentAsset();

    template<class Archive>
    void serialize(Archive&, u32 version);

    void processForImport();

    bool isIndexedMesh() const;
    void flattenToNonIndexedMesh();
    void convertToIndexedMesh();

    void optimize();

    void generateMeshlets();
    void generateTangents();

    #if PLATFORM_WINDOWS
    // NOTE: OMM-SDK only available for Windows..
    void generateOpacityMicroMap();
    #endif

    bool hasTextureCoordinates() const;
    bool hasTangents() const;

    // Returns true if this segment contains skinning data and thus can be used to create a skeletal mesh
    bool hasSkinningData() const;

    size_t vertexCount() const;
    std::vector<u8> assembleVertexData(const VertexLayout&) const;

    // Position vertex data for mesh segment
    std::vector<vec3> positions {};

    // TexCoord[0] vertex data for mesh segment
    std::vector<vec2> texcoord0s {};

    // Normal vertex data for mesh segment
    std::vector<vec3> normals {};

    // Tangent vertex data for mesh segment
    std::vector<vec4> tangents {};

    // Joint index vertex data for mesh segment (only for skinned meshes)
    std::vector<ark::tvec4<u16>> jointIndices {};

    // Joint weight vertex data for mesh segment (only for skinned meshes)
    std::vector<vec4> jointWeights {};

    // Indices used for indexed meshes (only needed for indexed meshes). For all vertex data types
    // the arrays must either be empty or have as many entries as the largest index in this array.
    std::vector<u32> indices {};

    // Meshlet data for this segment
    // TODO: Ideally move this data to a separate asset, so we don't need to always load this data (e.g. redundant when running without mesh shading)
    std::optional<MeshletDataAsset> meshletData {};

    // Opacity Micro-Map data for this segment
    // TODO: Ideally move this data to a separate asset, so we don't need to always load this data (e.g. redundant when running without ray tracing)
    std::optional<OpacityMicroMapDataAsset> opacityMicroMapData {};

    // Path to a material asset, used for rendering this mesh segment
    std::string material;

    // Not serialized, dynamic material asset, higher priority than `material`
    std::shared_ptr<MaterialAsset> dynamicMaterial {};

    // Not serialized, can be used to store whatever intermediate you want
    int userData { -1 };

private:
    void remapVertexData(std::vector<u32> const& remapTable, size_t newVertexCount);

};

class MeshLODAsset {
public:
    MeshLODAsset();
    ~MeshLODAsset();

    template<class Archive>
    void serialize(Archive&, u32 version);

    // List of mesh segments to be rendered (at least one needed)
    std::vector<MeshSegmentAsset> meshSegments {};
};

class MeshAsset final : public Asset<MeshAsset> {
public:
    MeshAsset();
    ~MeshAsset();

    static constexpr const char* AssetFileExtension = ".arkmsh";
    static constexpr std::array<char, 4> AssetMagicValue = { 'a', 'm', 's', 'h' };

    // Load a mesh asset (cached) from an .arkmsh file
    // TODO: Figure out how we want to return this! Basic type, e.g. MeshAsset*, or something reference counted, e.g. shared_ptr or manual ref-count?
    static MeshAsset* load(std::filesystem::path const& filePath);

    static MeshAsset* manage(std::unique_ptr<MeshAsset>&&);

    virtual bool readFromFile(std::filesystem::path const& filePath) override;
    virtual bool writeToFile(std::filesystem::path const& filePath, AssetStorage assetStorage) const override;

    template<class Archive>
    void serialize(Archive&, u32 version);

    std::vector<PhysicsMesh> createPhysicsMeshes(size_t lodIdx) const;
    PhysicsMesh createUnifiedPhysicsMesh(size_t lodIdx) const;

    // Mesh render data for each LODs (at least LOD0 needed)
    std::vector<MeshLODAsset> LODs {};

    // LOD settings for rendering
    u32 minLOD { 0 };
    u32 maxLOD { 99 };

    // Bounding box, pre object transform
    ark::aabb3 boundingBox {};

    // Bounding sphere, pre object transform
    geometry::Sphere boundingSphere {};

    // TODO: Add simple & complex physics data!
};

////////////////////////////////////////////////////////////////////////////////
// Serialization

#include "utility/EnumHelpers.h"
#include <cereal/cereal.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/types/vector.hpp>

enum class MeshAssetVersion : u32 {
    Initial = 0,
    AddOpacityMicroMaps,
    ////////////////////////////////////////////////////////////////////////////
    // Add new versions above this delimiter
    VersionCount,
    LatestVersion = VersionCount - 1
};

CEREAL_CLASS_VERSION(MeshletAsset, toUnderlying(MeshAssetVersion::LatestVersion))
CEREAL_CLASS_VERSION(MeshletDataAsset, toUnderlying(MeshAssetVersion::LatestVersion))
CEREAL_CLASS_VERSION(OpacityMicroMapDataAsset, toUnderlying(MeshAssetVersion::LatestVersion))
CEREAL_CLASS_VERSION(MeshSegmentAsset, toUnderlying(MeshAssetVersion::LatestVersion))
CEREAL_CLASS_VERSION(MeshLODAsset, toUnderlying(MeshAssetVersion::LatestVersion))
CEREAL_CLASS_VERSION(MeshAsset, toUnderlying(MeshAssetVersion::LatestVersion))

template<class Archive>
void serialize(Archive& archive, MeshletAsset& meshletAsset, u32 version)
{
    archive(cereal::make_nvp("firstIndex", meshletAsset.firstIndex));
    archive(cereal::make_nvp("triangleCount", meshletAsset.triangleCount));

    archive(cereal::make_nvp("firstVertex", meshletAsset.firstVertex));
    archive(cereal::make_nvp("vertexCount", meshletAsset.vertexCount));

    archive(cereal::make_nvp("center", meshletAsset.center));
    archive(cereal::make_nvp("radius", meshletAsset.radius));
}

template<class Archive>
void serialize(Archive& archive, MeshletDataAsset& meshletDataAsset, u32 version)
{
    archive(cereal::make_nvp("meshlets", meshletDataAsset.meshlets));
    archive(cereal::make_nvp("meshletVertexIndirection", meshletDataAsset.meshletVertexIndirection));
    archive(cereal::make_nvp("meshletIndices", meshletDataAsset.meshletIndices));
}

template<class Archive>
void serialize(Archive& archive, OpacityMicroMapDataAsset& ommDataAsset, u32 version)
{
    archive(cereal::make_nvp("ommSdkSerializedData", ommDataAsset.ommSdkSerializedData));
}

template<class Archive>
void MeshSegmentAsset::serialize(Archive& archive, u32 version)
{
    archive(CEREAL_NVP(positions));
    archive(CEREAL_NVP(texcoord0s));
    archive(CEREAL_NVP(normals));
    archive(CEREAL_NVP(tangents));

    archive(CEREAL_NVP(jointIndices));
    archive(CEREAL_NVP(jointWeights));

    archive(CEREAL_NVP(indices));

    archive(CEREAL_NVP(meshletData));
    if (version >= toUnderlying(MeshAssetVersion::AddOpacityMicroMaps)) {
        archive(CEREAL_NVP(opacityMicroMapData));
    }

    archive(CEREAL_NVP(material));
}

template<class Archive>
void MeshLODAsset::serialize(Archive& archive, u32 version)
{
    archive(CEREAL_NVP(meshSegments));
}

template<class Archive>
void MeshAsset::serialize(Archive& archive, u32 version)
{
    archive(CEREAL_NVP(name));

    archive(CEREAL_NVP(LODs));
    archive(CEREAL_NVP(minLOD));
    archive(CEREAL_NVP(maxLOD));

    archive(CEREAL_NVP(boundingBox));
    archive(CEREAL_NVP(boundingSphere));
}
