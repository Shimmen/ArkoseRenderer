#include "DebugDrawNode.h"

#include "core/Logging.h"
#include "rendering/GpuScene.h"
#include "rendering/Sprite.h"
#include "rendering/debug/DebugDrawer.h"
#include <imgui.h>

DebugDrawNode::DebugDrawNode()
{
    DebugDrawer::get().registerDebugDrawer(*this);
}

DebugDrawNode::~DebugDrawNode()
{
    DebugDrawer::get().unregisterDebugDrawer(*this);
}

void DebugDrawNode::drawGui()
{
    ImGui::Checkbox("Draw mesh bounding boxes", &m_shouldDrawInstanceBoundingBoxes);
}

RenderPipelineNode::ExecuteCallback DebugDrawNode::construct(GpuScene& scene, Registry& reg)
{
    VertexLayout debugDrawVertexLayout = { VertexComponent::Position3F, VertexComponent::Color3F };
    Shader debugDrawShader = Shader::createBasicRasterize("debug/debugDraw.vert", "debug/debugDraw.frag");

    BindingSet& cameraBindingSet = *reg.getBindingSet("SceneCameraSet");

    RenderTarget& renderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, reg.getTexture("SceneColor"), LoadOp::Load, StoreOp::Store },
                                                          { RenderTarget::AttachmentType::Depth, reg.getTexture("SceneDepth"), LoadOp::Load, StoreOp::Store } });

    RenderStateBuilder trianglesStateBuilder { renderTarget, debugDrawShader, debugDrawVertexLayout };
    trianglesStateBuilder.stateBindings().at(0, cameraBindingSet);
    trianglesStateBuilder.primitiveType = PrimitiveType::Triangles;
    trianglesStateBuilder.cullBackfaces = true;
    trianglesStateBuilder.writeDepth = true;
    trianglesStateBuilder.testDepth = true;

    RenderStateBuilder linesStateBuilder = trianglesStateBuilder;
    linesStateBuilder.primitiveType = PrimitiveType::LineSegments;
    linesStateBuilder.lineWidth = 3.0f;
    linesStateBuilder.writeDepth = false;
    linesStateBuilder.testDepth = false;

    RenderState& linesRenderState = reg.createRenderState(linesStateBuilder);
    RenderState& trianglesRenderState = reg.createRenderState(trianglesStateBuilder);

    m_lineVertexBuffer = &reg.createBuffer(LineVertexBufferSize, Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOnly);
    m_triangleVertexBuffer = &reg.createBuffer(TriangleVertexBufferSize, Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOnly);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        if (m_shouldDrawInstanceBoundingBoxes) {
            drawInstanceBoundingBoxes(scene);
        }

        if (m_lineVertices.size() > 0) {
            uploadBuffer.upload(m_lineVertices, *m_lineVertexBuffer);
        }

        if (m_triangleVertices.size() > 0) {
            uploadBuffer.upload(m_triangleVertices, *m_triangleVertexBuffer);
        }

        cmdList.executeBufferCopyOperations(uploadBuffer);

        uint32_t numLineVertices = static_cast<uint32_t>(m_lineVertices.size());
        ARKOSE_ASSERT(numLineVertices % 2 == 0);
        m_lineVertices.clear();

        uint32_t numTriangleVertices = static_cast<uint32_t>(m_triangleVertices.size());
        ARKOSE_ASSERT(numTriangleVertices % 3 == 0);
        m_triangleVertices.clear();

        if (numLineVertices > 0) {
            cmdList.beginRendering(linesRenderState);
            cmdList.draw(*m_lineVertexBuffer, numLineVertices);
            cmdList.endRendering();
        }

        if (numTriangleVertices > 0) {
            cmdList.beginRendering(trianglesRenderState);
            cmdList.draw(*m_triangleVertexBuffer, numTriangleVertices);
            cmdList.endRendering();
        }

    };
}

void DebugDrawNode::drawLine(vec3 p0, vec3 p1, vec3 color)
{
    if (m_lineVertices.size() == MaxNumLineSegments * 2) {
        ARKOSE_LOG(Warning, "Debug draw: maximum number of line segments reached, will not draw all requested lines.");
        return;
    }

    m_lineVertices.emplace_back(p0, color);
    m_lineVertices.emplace_back(p1, color);
}

void DebugDrawNode::drawBox(vec3 minPoint, vec3 maxPoint, vec3 color)
{
    vec3 p0 = vec3(minPoint.x, minPoint.y, minPoint.z);
    vec3 p1 = vec3(minPoint.x, minPoint.y, maxPoint.z);
    vec3 p2 = vec3(minPoint.x, maxPoint.y, minPoint.z);
    vec3 p3 = vec3(minPoint.x, maxPoint.y, maxPoint.z);
    vec3 p4 = vec3(maxPoint.x, minPoint.y, minPoint.z);
    vec3 p5 = vec3(maxPoint.x, minPoint.y, maxPoint.z);
    vec3 p6 = vec3(maxPoint.x, maxPoint.y, minPoint.z);
    vec3 p7 = vec3(maxPoint.x, maxPoint.y, maxPoint.z);

    // Bottom quad
    drawLine(p0, p1, color);
    drawLine(p1, p5, color);
    drawLine(p5, p4, color);
    drawLine(p4, p0, color);

    // Top quad
    drawLine(p2, p3, color);
    drawLine(p3, p7, color);
    drawLine(p7, p6, color);
    drawLine(p6, p2, color);

    // Vertical lines
    drawLine(p0, p2, color);
    drawLine(p1, p3, color);
    drawLine(p4, p6, color);
    drawLine(p5, p7, color);
}

void DebugDrawNode::drawSprite(Sprite sprite)
{
    m_triangleVertices.emplace_back(sprite.points[0], sprite.color);
    m_triangleVertices.emplace_back(sprite.points[2], sprite.color);
    m_triangleVertices.emplace_back(sprite.points[1], sprite.color);

    m_triangleVertices.emplace_back(sprite.points[2], sprite.color);
    m_triangleVertices.emplace_back(sprite.points[0], sprite.color);
    m_triangleVertices.emplace_back(sprite.points[3], sprite.color);
}

void DebugDrawNode::drawInstanceBoundingBoxes(GpuScene& scene)
{
    for (auto const& instance : scene.scene().staticMeshInstances()) {
        if (StaticMesh* staticMesh = scene.staticMeshForHandle(instance->mesh())) {
            ark::aabb3 transformedAABB = staticMesh->boundingBox().transformed(instance->transform().worldMatrix());
            drawBox(transformedAABB.min, transformedAABB.max, vec3(1.0f, 0.0f, 1.0f));
        }
    }
}
