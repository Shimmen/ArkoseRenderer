#pragma once

#include "asset/StaticMeshAsset.h"
#include "core/Handle.h"
#include "core/Types.h"
#include "core/math/Sphere.h"
#include "physics/HandleTypes.h"
#include "rendering/DrawKey.h"
#include "rendering/Material.h"
#include "rendering/backend/base/AccelerationStructure.h"
#include "rendering/backend/util/DrawCall.h" // remove me!
#include "rendering/meshlet/MeshletView.h"
#include "scene/Vertex.h"
#include <ark/aabb.h>
#include <ark/vector.h>
#include <string>
#include <vector>
#include <memory>

class GpuScene; // remove me!

DEFINE_HANDLE_TYPE(StaticMeshHandle)

using MeshMaterialResolver = std::function<MaterialHandle(MaterialAsset const*)>;

struct StaticMeshSegment {

    StaticMeshSegment(StaticMeshSegmentAsset const*, MaterialHandle, BlendMode, DrawKey);

    StaticMeshSegmentAsset const* asset { nullptr };

    // Handle to the static mesh that this segment is part of
    StaticMeshHandle staticMeshHandle {};

    // Material used for rendering this mesh segment
    MaterialHandle material {};

    // Shortcut to avoid retrieving the material just to check blend mode
    BlendMode blendMode { BlendMode::Opaque };

    // Draw key used to differentiate segments in terms of "draw calls"
    DrawKey drawKey {};

    // View into the meshlets that can be used to render this mesh
    std::optional<MeshletView> meshletView {};

    // Bottom level acceleration structure (optional; only needed for ray tracing)
    // TODO: Create a geometry per StaticMeshLOD and use the SBT to lookup materials for the segments.
    // For now we create one per segment so we can ensure one material per "draw" and keep it simple
    std::unique_ptr<BottomLevelAS> blas { nullptr };

    // TODO: Remove this, they are for the temporary transition period..
    void ensureDrawCallIsAvailable(const VertexLayout&, GpuScene&) const;
    const DrawCallDescription& drawCallDescription(const VertexLayout&, GpuScene&) const;
    mutable std::unordered_map<VertexLayout, DrawCallDescription> m_drawCallDescriptions;

};

struct StaticMeshLOD {

    explicit StaticMeshLOD(StaticMeshLODAsset const*);

    StaticMeshLODAsset const* asset { nullptr };

    // List of static mesh segments to be rendered (at least one needed)
    std::vector<StaticMeshSegment> meshSegments {};

};

class StaticMesh {
public:

    StaticMesh(StaticMeshAsset const*, MeshMaterialResolver&&);
    StaticMesh() = default;
    ~StaticMesh() = default;

    void setName(std::string name) { m_name = std::move(name); }
    std::string_view name() const { return m_name; }

    void setHandleToSelf(StaticMeshHandle);

    uint32_t numLODs() const { return static_cast<uint32_t>(m_lods.size()); }

    StaticMeshLOD& lodAtIndex(uint32_t idx) { return m_lods[idx]; }
    const StaticMeshLOD& lodAtIndex(uint32_t idx) const { return m_lods[idx]; }

    std::vector<StaticMeshLOD>& LODs(){ return m_lods; }
    const std::vector<StaticMeshLOD>& LODs() const { return m_lods; }

    ark::aabb3 boundingBox() const { return m_boundingBox; }
    geometry::Sphere boundingSphere() const { return m_boundingSphere; }

    StaticMeshAsset const* asset() const { return m_asset; }

    bool hasTranslucentSegments() const { return m_hasTranslucentSegments; }
    bool hasNonTranslucentSegments() const { return m_hasNonTranslucentSegments; }

private:

    // Optional asset that this is created from
    StaticMeshAsset const* m_asset { nullptr };

    // Optional name of the mesh, usually set when loaded from some source file
    std::string m_name {};

    // Static mesh render data for each LODs (at least LOD0 needed)
    std::vector<StaticMeshLOD> m_lods {};

    // LOD settings for rendering
    u32 m_minLod { 0 };
    u32 m_maxLod { UINT32_MAX };

    // Immutable bounding box, pre object transform
    ark::aabb3 m_boundingBox {};

    // Immutable bounding sphere, pre object transform
    geometry::Sphere m_boundingSphere {};

    // Physics representation of this static mesh (optional)
    // This would usually be a triangle-mesh shape 1:1 with the static mesh LOD data
    PhysicsShapeHandle m_complexPhysicsShape {};

    // Simple physics representation of this static mesh (optional)
    // This would usually be a simplified representation of the mesh (e.g. convex hull or box)
    PhysicsShapeHandle m_simplePhysicsShape {};

    bool m_hasTranslucentSegments { false };
    bool m_hasNonTranslucentSegments { true };

};
