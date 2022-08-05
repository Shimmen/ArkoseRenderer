#pragma once

#include "core/Handle.h"
#include "core/Types.h"
#include "core/math/Sphere.h"
#include "physics/HandleTypes.h"
#include "rendering/Material.h"
#include "rendering/backend/util/DrawCall.h" // remove me!
#include <ark/aabb.h>
#include <ark/vector.h>
#include <string>
#include <vector>

DEFINE_HANDLE_TYPE(StaticMeshHandle)

class GpuScene; // remove me!

struct StaticMeshSegment {

    // Position vertex data for mesh segment
    std::vector<vec3> positions {};

    // TexCoord[0] vertex data for mesh segment
    std::vector<vec2> texcoord0s {};

    // Normal vertex data for mesh segment
    std::vector<vec3> normals {};

    // Tangent vertex data for mesh segment
    std::vector<vec4> tangents {};

    // Indices used for indexed meshes (optional; only needed for indexed meshes)
    // For all required vertex data the arrays must have at least as many entries as the largest index in this array
    std::vector<uint32_t> indices {};

    // Material used for rendering this mesh segment
    MaterialHandle material {};

    size_t vertexCount() const;
    std::vector<uint8_t> assembleVertexData(const VertexLayout&) const;

    // TODO: Remove this, they are for the temporary transition period..
    void ensureDrawCallIsAvailable(const VertexLayout&, GpuScene&) const;
    const DrawCallDescription& drawCallDescription(const VertexLayout&, GpuScene&) const;
    mutable std::unordered_map<VertexLayout, DrawCallDescription> m_drawCallDescriptions;

};

struct StaticMeshLOD {

    // List of static mesh segments to be rendered (at least one needed)
    std::vector<StaticMeshSegment> meshSegments {};

    // Immutable bounding box, pre object transform
    ark::aabb3 boundingBox {};

    // Immutable bounding sphere, pre object transform
    geometry::Sphere boundingSphere {};

    // Physics representation of this LOD of the static mesh (optional)
    // This would usually be a triangle-mesh shape 1:1 with the static mesh LOD data
    PhysicsShapeHandle physicsShape {};

};

class StaticMesh {
    
public:

    StaticMesh() = default;
    ~StaticMesh() = default;

    // Just make the loaders access into the private bits so we can keep their interfaces nice and simple
    friend class GltfLoader;

    std::string_view name() const { return m_name; }

    uint32_t numLODs() const { return static_cast<uint32_t>(m_lods.size()); }
    
    StaticMeshLOD& lodAtIndex(uint32_t idx) { return m_lods[idx]; }
    const StaticMeshLOD& lodAtIndex(uint32_t idx) const { return m_lods[idx]; }
    
    std::vector<StaticMeshLOD>& LODs(){ return m_lods; }
    const std::vector<StaticMeshLOD>& LODs() const { return m_lods; }

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
    // TODO: Add these back!
    //uint32_t m_minLod { 0 };
    //uint32_t m_maxLod { UINT32_MAX };

    // Simple physics representation of this static mesh (optional)
    // This would usually be a simplified representation of the mesh (e.g. convex hull or box)
    PhysicsShapeHandle m_simplePhysicsShape {};

};
