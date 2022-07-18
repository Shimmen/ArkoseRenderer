#pragma once

#include "core/Handle.h"
#include "core/Types.h"
#include "core/math/Sphere.h"
#include "physics/HandleTypes.h"
#include "rendering/Material.h"
#include <ark/aabb.h>
#include <ark/vector.h>
#include <string>
#include <vector>

DEFINE_HANDLE_TYPE(StaticMeshHandle)

class StaticMeshSegment {

    // Position vertex data for mesh segment
    std::vector<vec3> m_positions {};

    // TexCoord[0] vertex data for mesh segment
    std::vector<vec2> m_texcoord0s {};

    // Normal vertex data for mesh segment
    std::vector<vec3> m_normals {};

    // Tangent vertex data for mesh segment
    std::vector<vec4> m_tangents {};

    // Indices used for indexed meshes (optional; only needed for indexed meshes)
    // For all required vertex data the arrays must have at least as many entries as the largest index in this array
    std::vector<uint32_t> m_indices {};

    // Material used for rendering this mesh segment
    MaterialHandle m_material {};

    // Just make the loaders access into the private bits so we can keep their interfaces nice and simple
    friend class GltfLoader;

};

class StaticMeshLOD {

    // List of static mesh segments to be rendered (at least one needed)
    std::vector<StaticMeshSegment> m_meshSegments {};

    // Immutable bounding box, pre object transform
    ark::aabb3 m_boundingBox {};

    // Immutable bounding sphere, pre object transform
    geometry::Sphere m_boundingSphere {};

    // Physics representation of this LOD of the static mesh (optional)
    // This would usually be a triangle-mesh shape 1:1 with the static mesh LOD data
    PhysicsShapeHandle m_physicsShape {};

    // Just make the loaders access into the private bits so we can keep their interfaces nice and simple
    friend class GltfLoader;

};

class StaticMesh {
    
public:

    StaticMesh() = default;
    ~StaticMesh() = default;

    // Just make the loaders access into the private bits so we can keep their interfaces nice and simple
    friend class GltfLoader;

    std::string_view name() const { return m_name; }

    uint32_t numLODs() const { return static_cast<uint32_t>(m_lods.size()); }
    const StaticMeshLOD& lodAtIndex(uint32_t idx) const { return m_lods[idx]; }

    void writeToFile(/* some file stream */) const;
    void readFromFile(/* some file stream */) const;

    static StaticMesh createFromFile(/* some file stream */)
    {
        StaticMesh staticMesh {};
        staticMesh.readFromFile(/* some file stream */);
        return staticMesh;
    }

private:

    // Optional name of the mesh, usually set when loaded from some source file
    std::string m_name {};

    // Static mesh render data for each LODs (at least LOD0 needed)
    std::vector<StaticMeshLOD> m_lods {};

    // LOD settings for rendering
    uint32_t m_minLod { 0 };
    uint32_t m_maxLod { UINT32_MAX };

    // Simple physics representation of this static mesh (optional)
    // This would usually be a simplified representation of the mesh (e.g. convex hull or box)
    PhysicsShapeHandle m_simplePhysicsShape {};

};
