#include "JoltVisualiser.h"

#if JPH_DEBUG_RENDERER

#include "core/Logging.h"
#include "rendering/debug/DebugDrawer.h"

JoltVisualiser::JoltVisualiser()
    : JPH::DebugRenderer()
{

}

JoltVisualiser::~JoltVisualiser() = default;

vec3 JoltVisualiser::joltColorToFloatColor(JPH::ColorArg color) const
{
    // TODO: Handle alpha?
    return vec3(color.r * 255.99f, color.g * 255.99f, color.b * 255.99f);
}

void JoltVisualiser::DrawLine(const JPH::Float3& inFrom, const JPH::Float3& inTo, JPH::ColorArg inColor)
{
    DebugDrawer::get().drawLine({ inFrom.x, inFrom.y, inFrom.z }, { inTo.x, inTo.y, inTo.z }, joltColorToFloatColor(inColor));
}

void JoltVisualiser::DrawTriangle(JPH::Vec3Arg inV1, JPH::Vec3Arg inV2, JPH::Vec3Arg inV3, JPH::ColorArg inColor)
{
    // TODO: Maybe make a more streamlined path for this?
    DebugDrawer::get().drawLine({ inV1.GetX(), inV1.GetY(), inV1.GetZ() }, { inV2.GetX(), inV2.GetY(), inV2.GetZ() }, joltColorToFloatColor(inColor));
    DebugDrawer::get().drawLine({ inV2.GetX(), inV2.GetY(), inV2.GetZ() }, { inV3.GetX(), inV3.GetY(), inV3.GetZ() }, joltColorToFloatColor(inColor));
    DebugDrawer::get().drawLine({ inV3.GetX(), inV3.GetY(), inV3.GetZ() }, { inV1.GetX(), inV1.GetY(), inV1.GetZ() }, joltColorToFloatColor(inColor));
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
