
// TODO!
//  - make subclass of StaticMesh or make separate one? Or give them shared subclass?

#pragma once

#include "rendering/StaticMesh.h"

class SkeletonAsset;

DEFINE_HANDLE_TYPE(SkeletalMeshHandle)

class SkeletalMesh {
public:
    SkeletalMesh(MeshAsset const*, SkeletonAsset const*, MeshMaterialResolver&&);
    SkeletalMesh() = default;
    ~SkeletalMesh() = default;

    StaticMesh const& staticMesh() const { return m_staticMesh; }
    StaticMesh& staticMesh() { return m_staticMesh; }

    void setName(std::string name) { staticMesh().setName(std::move(name)); }
    std::string_view name() const { return staticMesh().name(); }

    MeshAsset const* meshAsset() const { return m_meshAsset; }
    SkeletonAsset const* skeletonAsset() const { return m_skeletonAsset; }

private:
    // Mesh asset that this is created from
    MeshAsset const* m_meshAsset { nullptr };

    // Skeleton asset that this is created from
    SkeletonAsset const* m_skeletonAsset { nullptr };

    // The static mesh that is compatible with the skeleton
    StaticMesh m_staticMesh {};
};
