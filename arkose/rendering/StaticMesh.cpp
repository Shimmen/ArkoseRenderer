#include "StaticMesh.h"

#include "asset/MeshAsset.h"
#include "rendering/backend/base/AccelerationStructure.h"

StaticMeshSegment::StaticMeshSegment(MeshSegmentAsset const* inAsset, MaterialHandle inMaterial, BlendMode inBlendMode, DrawKey inDrawKey)
    : asset(inAsset)
    , material(inMaterial)
    , blendMode(inBlendMode)
    , drawKey(inDrawKey)
{
}

StaticMeshLOD::StaticMeshLOD(MeshLODAsset const* inAsset)
    : asset(inAsset)
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
        StaticMeshLOD& lod = m_lods.emplace_back(&lodAsset);
        for (auto& segmentAsset : lodAsset.meshSegments) {

            std::string const& materialAssetPath = std::string(segmentAsset.pathToMaterial());
            MaterialAsset* materialAsset = MaterialAsset::load(materialAssetPath);

            if (materialAsset->blendMode == BlendMode::Translucent) {
                m_hasTranslucentSegments |= true;
            } else {
                m_hasNonTranslucentSegments |= true;
            }

            DrawKey drawKey = DrawKey::generate(materialAsset);

            MaterialHandle materialHandle = materialResolver(materialAsset);
            lod.meshSegments.emplace_back(&segmentAsset, materialHandle, materialAsset->blendMode, drawKey);
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
