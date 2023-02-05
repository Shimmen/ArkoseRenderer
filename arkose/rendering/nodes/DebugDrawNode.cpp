#include "DebugDrawNode.h"

#include "core/Logging.h"
#include "rendering/GpuScene.h"
#include "rendering/Icon.h"
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
}

RenderPipelineNode::ExecuteCallback DebugDrawNode::construct(GpuScene& scene, Registry& reg)
{
    m_backend = &scene.backend();

    // Register default (white) texture for when no texture is requested for debug draw. The triangles will then simply take on the tint color.
    ShaderBinding defaultTextureBinding = ShaderBinding::sampledTexture(scene.whiteTexture(), ShaderStage::Fragment);
    m_whiteDebugDrawTexture = m_debugDrawTextures.add(scene.backend().createBindingSet({ defaultTextureBinding }));
    BindingSet& defaultTextureBindingSet = *m_debugDrawTextures.get(m_whiteDebugDrawTexture);

    VertexLayout vertexLayout = { VertexComponent::Position3F, VertexComponent::Color3F };
    Shader debugDrawShader = Shader::createBasicRasterize("debug/debugDraw.vert", "debug/debugDraw.frag",
                                                          { ShaderDefine::makeBool("WITH_TEXTURES", false) });

    VertexLayout vertexLayoutTextured = { VertexComponent::Position3F, VertexComponent::Color3F, VertexComponent::TexCoord2F };
    Shader debugDrawShaderTextured = Shader::createBasicRasterize("debug/debugDraw.vert", "debug/debugDraw.frag",
                                                                  { ShaderDefine::makeBool("WITH_TEXTURES", true) });

    BindingSet& cameraBindingSet = *reg.getBindingSet("SceneCameraSet");

    RenderTarget& renderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, reg.getTexture("SceneColor"), LoadOp::Load, StoreOp::Store },
                                                          { RenderTarget::AttachmentType::Depth, reg.getTexture("SceneDepth"), LoadOp::Load, StoreOp::Store } });


    RenderStateBuilder linesStateBuilder { renderTarget, debugDrawShader, vertexLayout };
    linesStateBuilder.stateBindings().at(0, cameraBindingSet);
    linesStateBuilder.primitiveType = PrimitiveType::LineSegments;
    linesStateBuilder.lineWidth = 3.0f;
    linesStateBuilder.writeDepth = false;
    linesStateBuilder.testDepth = false;

    RenderStateBuilder trianglesStateBuilder { renderTarget, debugDrawShaderTextured, vertexLayoutTextured };
    trianglesStateBuilder.stateBindings().at(0, cameraBindingSet);
    trianglesStateBuilder.stateBindings().at(1, defaultTextureBindingSet);
    trianglesStateBuilder.primitiveType = PrimitiveType::Triangles;
    trianglesStateBuilder.cullBackfaces = true;
    trianglesStateBuilder.writeDepth = true;
    trianglesStateBuilder.testDepth = true;

    RenderState& linesRenderState = reg.createRenderState(linesStateBuilder);
    RenderState& trianglesRenderState = reg.createRenderState(trianglesStateBuilder);

    m_lineVertexBuffer = &reg.createBuffer(LineVertexBufferSize, Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOnly);
    m_triangleVertexBuffer = &reg.createBuffer(TriangleVertexBufferSize, Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOnly);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        if (m_lineVertices.size() > 0) {
            uploadBuffer.upload(m_lineVertices, *m_lineVertexBuffer);
        }

        if (m_triangleVertices.size() > 0) {
            uploadBuffer.upload(m_triangleVertices, *m_triangleVertexBuffer);
        }

        cmdList.executeBufferCopyOperations(uploadBuffer);

        uint32_t numLineVertices = narrow_cast<u32>(m_lineVertices.size());
        ARKOSE_ASSERT(numLineVertices % 2 == 0);
        m_lineVertices.clear();

        uint32_t numTriangleVertices = narrow_cast<u32>(m_triangleVertices.size());
        ARKOSE_ASSERT(numTriangleVertices % 3 == 0);
        m_triangleVertices.clear();

        if (numLineVertices > 0) {
            cmdList.beginRendering(linesRenderState);
            cmdList.draw(*m_lineVertexBuffer, numLineVertices);
            cmdList.endRendering();
        }

        if (numTriangleVertices > 0) {
            cmdList.beginRendering(trianglesRenderState);
            for (DebugDrawMesh const& mesh : m_debugDrawMeshes) {
                cmdList.bindSet(*m_debugDrawTextures.get(mesh.textureBindingSetHandle), 1);
                cmdList.draw(*m_triangleVertexBuffer, mesh.numVertices, mesh.firstVertex);
            }
            cmdList.endRendering();
        }

        // Clear out all transient debug draw meshes
        for (DebugDrawMesh const& mesh : m_debugDrawMeshes) {
            m_debugDrawTextures.removeReference(mesh.textureBindingSetHandle, appState.frameIndex());
        }
        m_debugDrawMeshes.clear();

        m_debugDrawTextures.processDeferredDeletes(appState.frameIndex(), 3, [](DebugTextureBindingSetHandle handle, std::unique_ptr<BindingSet>& textureBindingSet) {
            textureBindingSet.reset();
        });
    };
}

void DebugDrawNode::drawLine(vec3 p0, vec3 p1, vec3 color)
{
    if (m_lineVertices.size() + 2 > MaxNumLineSegments * 2) {
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

void DebugDrawNode::drawIcon(IconBillboard icon, vec3 tint)
{
    if (m_triangleVertices.size() + 6 > MaxNumTriangles * 3) {
        ARKOSE_LOG(Warning, "Debug draw: maximum number of triangles reached, will not draw all requested icons.");
        return;
    }

    m_debugDrawMeshes.push_back(DebugDrawMesh { .numVertices = 6,
                                                .firstVertex = narrow_cast<u32>(m_triangleVertices.size()),
                                                .textureBindingSetHandle = createIconTextureBindingSet(&icon.icon()) });

    auto const& ps = icon.positions();
    auto const& uvs = icon.texCoords();

    m_triangleVertices.emplace_back(ps[0], tint, uvs[0]);
    m_triangleVertices.emplace_back(ps[2], tint, uvs[2]);
    m_triangleVertices.emplace_back(ps[1], tint, uvs[1]);

    m_triangleVertices.emplace_back(ps[0], tint, uvs[0]);
    m_triangleVertices.emplace_back(ps[3], tint, uvs[3]);
    m_triangleVertices.emplace_back(ps[2], tint, uvs[2]);
}

DebugTextureBindingSetHandle DebugDrawNode::createIconTextureBindingSet(Icon const* icon)
{
    if (icon) {
        return createDebugTextureBindingSet(icon->texture());
    } else {
        return createDebugTextureBindingSet(nullptr);
    }
}

DebugTextureBindingSetHandle DebugDrawNode::createDebugTextureBindingSet(Texture const* texture)
{
    if (texture == nullptr) {
        m_debugDrawTextures.addReference(m_whiteDebugDrawTexture);
        return m_whiteDebugDrawTexture;
    }

    ShaderBinding textureBinding = ShaderBinding::sampledTexture(*texture, ShaderStage::Fragment);
    return m_debugDrawTextures.add(m_backend->createBindingSet({ textureBinding }));
}
