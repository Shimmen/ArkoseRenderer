#include "HairMesh.h"

#include "asset/HairAsset.h"

HairMesh::HairMesh(HairAsset const* hairAsset)
    : m_hairAsset(hairAsset)
{
}

HairMesh::~HairMesh() = default;

DrawCallDescription HairMesh::drawCallDescription() const
{
    // TODO: This should be accessible directly in HairAsset, not something we have to compute at runtime.
    u32 indexCount = 0;
    for (u32 strandIdx = 0; strandIdx < hairAsset()->strandCount; strandIdx++) {
        // e.g. for a strand with 2 segments (3 points) we need 4 indices to draw it as a line strip (0,1,2,z) (z=primitive reset)
        indexCount += hairAsset()->segmentCountForStrand(strandIdx) + 1 + 1;
    }

    DrawCallDescription drawCall {};
    drawCall.type = DrawCallDescription::Type::Indexed;
    drawCall.vertexOffset = hairVertexAlloc.offset;
    drawCall.vertexCount = hairAsset()->pointCount;
    drawCall.firstIndex = indexAlloc.offset;
    drawCall.indexCount = indexCount;

    return drawCall;
}
