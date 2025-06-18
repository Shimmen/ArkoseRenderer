#include "StaticMesh.h"

#include "asset/MeshAsset.h"
#include "rendering/GpuScene.h"
#include "rendering/backend/base/AccelerationStructure.h"

StaticMeshSegment::StaticMeshSegment(StaticMeshLOD& parent, MeshSegmentAsset const* inAsset, MaterialHandle inMaterial, BlendMode inBlendMode, DrawKey inDrawKey)
    : asset(inAsset)
    , material(inMaterial)
    , blendMode(inBlendMode)
    , drawKey(inDrawKey)
    , m_lod(parent)
{
}

void StaticMeshSegment::setMaterial(MaterialAsset* materialAsset, GpuScene& scene)
{
    MaterialHandle oldMaterial = material;
    material = scene.registerMaterial(materialAsset);

    if (blendMode != materialAsset->blendMode) {
        blendMode = materialAsset->blendMode;

        if (materialAsset->blendMode == BlendMode::Translucent) {
            m_lod.m_mesh.m_hasTranslucentSegments |= true;
        } else {
            m_lod.m_mesh.m_hasNonTranslucentSegments |= true;
        }
    }

    drawKey = DrawKey::generate(materialAsset);

    scene.notifyStaticMeshHasChanged(staticMeshHandle);
    scene.unregisterMaterial(oldMaterial);
}

StaticMeshLOD::StaticMeshLOD(StaticMesh& parent, MeshLODAsset const* inAsset)
    : asset(inAsset)
    , m_mesh(parent)
{
}

StaticMesh::StaticMesh(MeshAsset const* asset, MeshMaterialResolver&& materialResolver)
    : m_asset(asset)
    , m_name(asset->name)
    , m_minLod(asset->minLOD)
    , m_maxLod(asset->maxLOD)
    , m_boundingBox(asset->boundingBox)
    , m_boundingSphere(asset->boundingSphere)
{
    for (MeshLODAsset const& lodAsset : asset->LODs) {
        StaticMeshLOD& lod = m_lods.emplace_back(*this, &lodAsset);
        for (auto& segmentAsset : lodAsset.meshSegments) {

            MaterialAsset* materialAsset = nullptr;
            if (segmentAsset.dynamicMaterial) {
                materialAsset = segmentAsset.dynamicMaterial.get();
            } else {
                std::string const& materialAssetPath = std::string(segmentAsset.material);
                materialAsset = MaterialAsset::load(materialAssetPath);
            }
            ARKOSE_ASSERT(materialAsset);

            if (materialAsset->blendMode == BlendMode::Translucent) {
                m_hasTranslucentSegments |= true;
            } else {
                m_hasNonTranslucentSegments |= true;
            }

            DrawKey drawKey = DrawKey::generate(materialAsset);

            MaterialHandle materialHandle = materialResolver(materialAsset);
            lod.meshSegments.emplace_back(lod, &segmentAsset, materialHandle, materialAsset->blendMode, drawKey);
        }
    }
}

void StaticMesh::setHandleToSelf(StaticMeshHandle handle)
{
    for (StaticMeshLOD& lod : m_lods) {
        for (StaticMeshSegment& segment : lod.meshSegments) {
            // For now, only the segments need the handle
            segment.staticMeshHandle = handle;
        }
    }
}
