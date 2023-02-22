#include "StaticMesh.h"

#include "asset/StaticMeshAsset.h"
#include "rendering/backend/base/AccelerationStructure.h"
#include "rendering/GpuScene.h"

StaticMeshSegment::StaticMeshSegment(StaticMeshSegmentAsset const* inAsset, MaterialHandle inMaterial, BlendMode inBlendMode)
    : asset(inAsset)
    , material(inMaterial)
    , blendMode(inBlendMode)
{
}

StaticMeshLOD::StaticMeshLOD(StaticMeshLODAsset const* inAsset)
    : asset(inAsset)
{
}

StaticMesh::StaticMesh(StaticMeshAsset const* asset, MeshMaterialResolver&& materialResolver)
    : m_asset(asset)
    , m_name(asset->name)
    , m_minLod(asset->minLOD)
    , m_maxLod(asset->maxLOD)
    , m_boundingBox(asset->boundingBox)
    , m_boundingSphere(asset->boundingSphere)
{
    for (StaticMeshLODAsset const& lodAsset : asset->LODs) {
        StaticMeshLOD& lod = m_lods.emplace_back(&lodAsset);
        for (auto& segmentAsset : lodAsset.meshSegments) {

            std::string const& materialAssetPath = std::string(segmentAsset.pathToMaterial());
            MaterialAsset* materialAsset = MaterialAsset::loadFromArkmat(materialAssetPath);

            if (materialAsset->blendMode == BlendMode::Translucent) {
                m_hasTranslucentSegments |= true;
            } else {
                m_hasNonTranslucentSegments |= true;
            }

            MaterialHandle materialHandle = materialResolver(materialAsset);
            lod.meshSegments.emplace_back(&segmentAsset, materialHandle, materialAsset->blendMode);
        }
    }
}

void StaticMeshSegment::ensureDrawCallIsAvailable(const VertexLayout& layout, GpuScene& scene) const
{
    SCOPED_PROFILE_ZONE();
    // Will create the relevant buffers & set their data (if it doesn't already exist)
    drawCallDescription(layout, scene);
}

const DrawCallDescription& StaticMeshSegment::drawCallDescription(const VertexLayout& layout, GpuScene& scene) const
{
    SCOPED_PROFILE_ZONE();

    auto entry = m_drawCallDescriptions.find(layout);
    if (entry != m_drawCallDescriptions.end())
        return entry->second;

    // This specific vertex layout has not yet been fitted to the vertex buffer but there are at least one other layout setup.
    // All subsequent layouts should replicate the offsets etc. since it means we can reuse index data & also can expect that
    // vertex layouts line up w.r.t. the DrawCallDescription. This is good if you e.g. cull, then z-prepass with position-only,
    // and then draw objects normally with a full layout. If they line up we can use the indirect culling draw commands for both!

    std::optional<DrawCallDescription> previousToAlignWith {};
    if (m_drawCallDescriptions.size() > 0) {
        previousToAlignWith = m_drawCallDescriptions.begin()->second;
    }

    DrawCallDescription drawCallDescription = scene.fitVertexAndIndexDataForMesh({}, *this, layout, previousToAlignWith);

    m_drawCallDescriptions[layout] = drawCallDescription;
    return m_drawCallDescriptions[layout];
}
