#include "DebugDrawNode.h"

#include "core/Logging.h"
#include "rendering/GpuScene.h"
#include "rendering/Icon.h"
#include "rendering/Skeleton.h"
#include "rendering/debug/DebugDrawer.h"
#include <ark/core.h>
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

    Texture* targetTex = reg.outputTexture();
    Texture* sceneDepthTex = reg.getTexture("SceneDepth");

    std::vector<RenderTarget::Attachment> attachments;
    attachments.push_back({ RenderTarget::AttachmentType::Color0, targetTex, LoadOp::Load, StoreOp::Store });
    if (sceneDepthTex->extent() == targetTex->extent()) {
        attachments.push_back({ RenderTarget::AttachmentType::Depth, sceneDepthTex, LoadOp::Load, StoreOp::Store });
    } else {
        // TODO: Create an appropriate depth texture, and upscale the depth to that target with a simple nearest-neighbor upscale
        ARKOSE_LOG(Error, "DEBUG DRAWING UPSCALING HACK: Since the debug drawing needs to depth write it can't use the non-upscaled "
                          "depth texture. For now, when using upscaling, we will simply not do any depth testing. This can be fixed "
                          "by copying the depth over to an upscaled texture (nearest sampling) and using that instead. Since it's just"
                          "for debug drawing, nothing else will need this texture afterwards, so it makes sense to do it just here");
    }
    RenderTarget& renderTarget = reg.createRenderTarget(attachments);

    RenderStateBuilder linesStateBuilder { renderTarget, debugDrawShader, vertexLayout };
    linesStateBuilder.stateBindings().at(0, cameraBindingSet);
    linesStateBuilder.primitiveType = PrimitiveType::LineSegments;
    linesStateBuilder.lineWidth = 1.0f;
    linesStateBuilder.writeDepth = false;
    linesStateBuilder.testDepth = false;

    RenderStateBuilder arrowsStateBuilder = linesStateBuilder;
    arrowsStateBuilder.lineWidth = 8.0f;
    arrowsStateBuilder.writeDepth = true;
    arrowsStateBuilder.testDepth = true;

    RenderStateBuilder trianglesStateBuilder { renderTarget, debugDrawShaderTextured, vertexLayoutTextured };
    trianglesStateBuilder.stateBindings().at(0, cameraBindingSet);
    trianglesStateBuilder.stateBindings().at(1, defaultTextureBindingSet);
    trianglesStateBuilder.primitiveType = PrimitiveType::Triangles;
    trianglesStateBuilder.cullBackfaces = true;
    trianglesStateBuilder.writeDepth = true;
    trianglesStateBuilder.testDepth = true;

    RenderState& linesRenderState = reg.createRenderState(linesStateBuilder);
    RenderState& arrowsRenderState = reg.createRenderState(arrowsStateBuilder);
    RenderState& trianglesRenderState = reg.createRenderState(trianglesStateBuilder);

    m_lineVertexBuffer = &reg.createBuffer(LineVertexBufferSize, Buffer::Usage::Vertex);
    m_arrowVertexBuffer = &reg.createBuffer(ArrowVertexBufferSize, Buffer::Usage::Vertex);
    m_triangleVertexBuffer = &reg.createBuffer(TriangleVertexBufferSize, Buffer::Usage::Vertex);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        if (m_lineVertices.size() > 0) {
            uploadBuffer.upload(m_lineVertices, *m_lineVertexBuffer);
        }

        if (m_arrowVertices.size() > 0) {
            uploadBuffer.upload(m_arrowVertices, *m_arrowVertexBuffer);
        }

        if (m_triangleVertices.size() > 0) {
            uploadBuffer.upload(m_triangleVertices, *m_triangleVertexBuffer);
        }

        cmdList.executeBufferCopyOperations(uploadBuffer);

        uint32_t numLineVertices = narrow_cast<u32>(m_lineVertices.size());
        ARKOSE_ASSERT(numLineVertices % 2 == 0);
        m_lineVertices.clear();

        uint32_t numArrowVertices = narrow_cast<u32>(m_arrowVertices.size());
        ARKOSE_ASSERT(numArrowVertices % 2 == 0);
        m_arrowVertices.clear();

        uint32_t numTriangleVertices = narrow_cast<u32>(m_triangleVertices.size());
        ARKOSE_ASSERT(numTriangleVertices % 3 == 0);
        m_triangleVertices.clear();

        if (numLineVertices > 0) {
            cmdList.beginRendering(linesRenderState);
            cmdList.bindVertexBuffer(*m_lineVertexBuffer, linesRenderState.vertexLayout().packedVertexSize(), 0);
            cmdList.draw(numLineVertices);
            cmdList.endRendering();
        }

        if (numArrowVertices > 0) {
            cmdList.beginRendering(arrowsRenderState);
            cmdList.bindVertexBuffer(*m_arrowVertexBuffer, arrowsRenderState.vertexLayout().packedVertexSize(), 0);
            cmdList.draw(numArrowVertices);
            cmdList.endRendering();
        }

        if (numTriangleVertices > 0) {
            cmdList.beginRendering(trianglesRenderState);
            for (DebugDrawMesh const& mesh : m_debugDrawMeshes) {
                cmdList.bindTextureSet(*m_debugDrawTextures.get(mesh.textureBindingSetHandle), 1);
                cmdList.bindVertexBuffer(*m_triangleVertexBuffer, trianglesRenderState.vertexLayout().packedVertexSize(), 0);
                cmdList.draw(mesh.numVertices, mesh.firstVertex);
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

void DebugDrawNode::drawLine(vec3 p0, vec3 p1, Color color)
{
    if (m_lineVertices.size() + 2 > MaxNumLineSegments * 2) {
        ARKOSE_LOG(Warning, "Debug draw: maximum number of line segments reached, will not draw all requested lines.");
        return;
    }

    m_lineVertices.emplace_back(p0, color.asVec3());
    m_lineVertices.emplace_back(p1, color.asVec3());
}

void DebugDrawNode::drawArrow(vec3 origin, vec3 direction, float length, Color color)
{
    if (m_arrowVertices.size() + 2 > MaxNumArrows * 2) {
        ARKOSE_LOG(Warning, "Debug draw: maximum number of arrows segments reached, will not draw all requested arrows.");
        return;
    }

    m_arrowVertices.emplace_back(origin, color.asVec3());
    m_arrowVertices.emplace_back(origin + length * direction, color.asVec3());
}

void DebugDrawNode::drawBox(vec3 minPoint, vec3 maxPoint, Color color)
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

void DebugDrawNode::drawSphere(vec3 center, float radius, Color color)
{
    using namespace ark;

    constexpr i32 sectors = 9;
    constexpr i32 rings = 9;

    float const R = 1.0f / static_cast<float>(rings - 1);
    float const S = 1.0f / static_cast<float>(sectors - 1);

    std::vector<vec3> positions {};
    positions.reserve(rings * sectors * 3);

    for (int r = 0; r < rings; r++) {
        for (int s = 0; s < sectors; s++) {
            float y = std::sin(-HALF_PI + PI * r * R);
            float x = std::cos(TWO_PI * s * S) * std::sin(PI * r * R);
            float z = std::sin(TWO_PI * s * S) * std::sin(PI * r * R);
            vec3 vertex = center + radius * vec3(x, y, z);
            positions.push_back(vertex);
        }
    }

    for (int r = 0; r < rings - 1; ++r) {
        // NOTE: We only need to draw two of the four sides of each face, as the next face will cover those edges.

        for (int s = 0; s < sectors - 1; ++s) {
            int i0 = r * sectors + s;
            int i1 = r * sectors + (s + 1);
            int i2 = (r + 1) * sectors + (s + 1);
            //int i3 = (r + 1) * sectors + s;

            vec3 const& p0 = positions[i0];
            vec3 const& p1 = positions[i1];
            vec3 const& p2 = positions[i2];
            //vec3 const& p3 = positions[i3];

            drawLine(p0, p1, color);
            drawLine(p1, p2, color);
        }
    }
}

void DebugDrawNode::drawIcon(IconBillboard const& icon, Color tint)
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

    vec3 tintVec3 = tint.asVec3();

    m_triangleVertices.emplace_back(ps[0], tintVec3, uvs[0]);
    m_triangleVertices.emplace_back(ps[2], tintVec3, uvs[2]);
    m_triangleVertices.emplace_back(ps[1], tintVec3, uvs[1]);

    m_triangleVertices.emplace_back(ps[0], tintVec3, uvs[0]);
    m_triangleVertices.emplace_back(ps[3], tintVec3, uvs[3]);
    m_triangleVertices.emplace_back(ps[2], tintVec3, uvs[2]);
}

void DebugDrawNode::drawSkeleton(Skeleton const& skeleton, mat4 rootTransform, Color color)
{
    std::function<void(SkeletonJoint const&, vec3)> recursivelyDrawJoints = [&](SkeletonJoint const& joint, vec3 previousJointPosition) {
        mat4 jointTransform = rootTransform * joint.transform().worldMatrix();
        vec3 jointPosition = jointTransform.w.xyz();

        drawSphere(jointPosition, 0.01f, color);
        drawLine(previousJointPosition, jointPosition, color);

        if (joint.childJoints().size() == 0) { 
            // Draw end-joints as a xyz axis visualization (is there a nicer way of doing this? probably..)
            drawLine(jointPosition, jointPosition + jointTransform.x.xyz() * 0.1f, Colors::red);
            drawLine(jointPosition, jointPosition + jointTransform.y.xyz() * 0.1f, Colors::green);
            drawLine(jointPosition, jointPosition + jointTransform.z.xyz() * 0.1f, Colors::blue);
        }

        for (SkeletonJoint const& childJoint : joint.childJoints()) {
            recursivelyDrawJoints(childJoint, jointPosition);
        }
    };

    vec3 rootPosition = rootTransform.w.xyz();
    recursivelyDrawJoints(skeleton.rootJoint(), rootPosition);
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
