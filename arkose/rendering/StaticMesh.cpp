#include "StaticMesh.h"

#include "asset/MeshAsset.h"
#include "rendering/backend/base/AccelerationStructure.h"

StaticMeshSegment::StaticMeshSegment(StaticMeshLOD& parent, MeshSegmentAsset const* inAsset, MaterialHandle inMaterial, BlendMode inBlendMode, DrawKey inDrawKey)
    : asset(inAsset)
    , material(inMaterial)
    , blendMode(inBlendMode)
    , drawKey(inDrawKey)
    , m_lod(parent)
{
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
            if (segmentAsset.hasPathToMaterial()) {
                std::string const& materialAssetPath = std::string(segmentAsset.pathToMaterial());
                materialAsset = MaterialAsset::load(materialAssetPath);
            } else {
                // TODO: Don't use a std::variant like this, just always serialize a path but allow assigning
                // a dynamic material in runtime. When we save and there's a dynamic one set, write that to
                // file and then serialize the path to that file. Will make this whole thing more explicit.
                materialAsset = segmentAsset.dynamicMaterialAsset().lock().get();
            }

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
