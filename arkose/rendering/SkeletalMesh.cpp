#include "SkeletalMesh.h"

#include "asset/MeshAsset.h"
#include "asset/SkeletonAsset.h"

SkeletalMesh::SkeletalMesh(MeshAsset const* meshAsset, SkeletonAsset const* skeletonAsset, MeshMaterialResolver&& materialResolver)
    : m_meshAsset(meshAsset)
    , m_skeletonAsset(skeletonAsset)
    , m_underlyingMesh(meshAsset, std::move(materialResolver))
{
}
