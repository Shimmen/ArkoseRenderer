#include "JoltVisualiser.h"

#if JPH_DEBUG_RENDERER

#include "core/Logging.h"

JoltVisualiser::JoltVisualiser()
    : JPH::DebugRenderer()
{

}

JoltVisualiser::~JoltVisualiser()
{
}


void JoltVisualiser::drawStuff(/* ... */)
{
    // TODO Here's what arkose calls into / out from for actually rendering stuff
}

void JoltVisualiser::DrawLine(const JPH::Float3& inFrom, const JPH::Float3& inTo, JPH::ColorArg inColor)
{
    ARKOSE_LOG(Info, "DrawLine");
}

void JoltVisualiser::DrawTriangle(JPH::Vec3Arg inV1, JPH::Vec3Arg inV2, JPH::Vec3Arg inV3, JPH::ColorArg inColor)
{
    ARKOSE_LOG(Info, "DrawTriangle");
}

JPH::DebugRenderer::Batch JoltVisualiser::CreateTriangleBatch(const JPH::DebugRenderer::Triangle* inTriangles, int inTriangleCount)
{
    ARKOSE_LOG(Info, "CreateTriangleBatch");
    uint32_t batchId = m_nextBatchId++;
    return new ArkoseBatch(batchId);
}

JPH::DebugRenderer::Batch JoltVisualiser::CreateTriangleBatch(const JPH::DebugRenderer::Vertex* inVertices, int inVertexCount, const uint32_t* inIndices, int inIndexCount)
{
    ARKOSE_LOG(Info, "CreateTriangleBatch");
    uint32_t batchId = m_nextBatchId++;
    return new ArkoseBatch(batchId);
}

void JoltVisualiser::DrawGeometry(JPH::Mat44Arg modelMatrix, const JPH::AABox& worldSpaceBounds, float inLODScaleSq, JPH::ColorArg, const GeometryRef&, ECullMode, ECastShadow, EDrawMode)
{
    ARKOSE_LOG(Info, "DrawGeometry");
}

void JoltVisualiser::DrawText3D(JPH::Vec3Arg inPosition, const std::string_view& inString, JPH::ColorArg inColor, float inHeight = 0.5f)
{
    ARKOSE_LOG(Info, "DrawText3D");
}

#endif // JPH_DEBUG_RENDERER
