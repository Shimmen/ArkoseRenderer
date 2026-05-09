#include "HairMesh.h"

#include "asset/HairAsset.h"

HairMesh::HairMesh(HairAsset const* hairAsset)
    : m_hairAsset(hairAsset)
{
}

HairMesh::~HairMesh() = default;

DrawCallDescription HairMesh::drawCallDescription() const
{
    DrawCallDescription drawCall {};
    drawCall.type = DrawCallDescription::Type::Indexed;
    drawCall.vertexOffset = hairVertexAlloc.offset;
    drawCall.vertexCount = static_cast<u32>(hairAsset()->positions.size());
    drawCall.firstIndex = indexAlloc.offset;
    drawCall.indexCount = static_cast<u32>(hairAsset()->indices.size());

    return drawCall;
}
