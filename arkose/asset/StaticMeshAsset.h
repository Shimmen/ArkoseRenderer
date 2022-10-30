#pragma once

#include "asset/AssetHelpers.h"
#include "asset/MaterialAsset.h"
#include "core/Types.h"
#include "core/math/Sphere.h"
#include <ark/aabb.h>
#include <vector>
#include <string>
#include <string_view>
#include <variant>

struct PhysicsMesh;

class StaticMeshSegmentAsset {
public:
    StaticMeshSegmentAsset();
    ~StaticMeshSegmentAsset();

    template<class Archive>
    void serialize(Archive&);

    bool hasPathToMaterial() const { return std::holds_alternative<std::string>(material); }
    void setPathToMaterial(std::string path) { material = std::move(path); }
    std::string_view pathToMaterial() const
    {
        ARKOSE_ASSERT(hasPathToMaterial());
        return std::get<std::string>(material);
    }

    // Position vertex data for mesh segment
    std::vector<vec3> positions {};

    // TexCoord[0] vertex data for mesh segment
    std::vector<vec2> texcoord0s {};

    // Normal vertex data for mesh segment
    std::vector<vec3> normals {};

    // Tangent vertex data for mesh segment
    std::vector<vec4> tangents {};

    // Indices used for indexed meshes (only needed for indexed meshes). For all vertex data types
    // the arrays must either be empty or have as many entries as the largest index in this array.
    std::vector<u32> indices {};

    // Path to a material or a material asset directly, used for rendering this mesh segment
    std::variant<std::string, std::weak_ptr<MaterialAsset>> material;

    // Not serialized, can be used to store whatever intermediate you want
    int userData { -1 };

};

class StaticMeshLODAsset {
public:
    StaticMeshLODAsset();
    ~StaticMeshLODAsset();

    template<class Archive>
    void serialize(Archive&);

    // List of static mesh segments to be rendered (at least one needed)
    std::vector<StaticMeshSegmentAsset> meshSegments {};
};

class StaticMeshAsset {
public:
    StaticMeshAsset();
    ~StaticMeshAsset();

    static constexpr const char* AssetFileExtension = "arkmsh";
    static constexpr std::array<char, 4> AssetMagicValue = { 'a', 'm', 's', 'h' };

    // Load a static mesh asset (cached) from an .arkmsh file
    // TODO: Figure out how we want to return this! Basic type, e.g. StaticMeshAsset*, or something reference counted, e.g. shared_ptr or manual ref-count?
    static StaticMeshAsset* loadFromArkmsh(std::string const& filePath);

    bool writeToArkmsh(std::string_view filePath, AssetStorage);

    template<class Archive>
    void serialize(Archive&);

    std::string_view assetFilePath() const { return m_assetFilePath; }

    std::vector<PhysicsMesh> createPhysicsMeshes(size_t lodIdx) const;

    // Name of the mesh, usually set when loaded from some source file
    std::string name {};

    // Static mesh render data for each LODs (at least LOD0 needed)
    std::vector<StaticMeshLODAsset> LODs {};

    // LOD settings for rendering
    u32 minLOD { 0 };
    u32 maxLOD { 99 };

    // Bounding box, pre object transform
    ark::aabb3 boundingBox {};

    // Bounding sphere, pre object transform
    geometry::Sphere boundingSphere {};

    // TODO: Add simple & complex physics data!

    // Not serialized, can be used to store whatever intermediate you want
    int userData { -1 };

private:
    std::string m_assetFilePath {};
};

////////////////////////////////////////////////////////////////////////////////
// Serialization

#include <cereal/cereal.hpp>
#include <cereal/types/variant.hpp>
#include <cereal/types/vector.hpp>

template<class Archive>
void StaticMeshSegmentAsset::serialize(Archive& archive)
{
    archive(CEREAL_NVP(positions));
    archive(CEREAL_NVP(texcoord0s));
    archive(CEREAL_NVP(normals));
    archive(CEREAL_NVP(tangents));

    archive(CEREAL_NVP(indices));

    archive(CEREAL_NVP(material));
}

template<class Archive>
void StaticMeshLODAsset::serialize(Archive& archive)
{
    archive(CEREAL_NVP(meshSegments));
}

template<class Archive>
void StaticMeshAsset::serialize(Archive& archive)
{
    archive(CEREAL_NVP(name));

    archive(CEREAL_NVP(LODs));
    archive(CEREAL_NVP(minLOD));
    archive(CEREAL_NVP(maxLOD));

    archive(CEREAL_NVP(boundingBox));
    archive(CEREAL_NVP(boundingSphere));
}
