#include "DebugForwardNode.h"

#include "ForwardRenderNode.h"
#include "LightData.h"
#include "ShadowMapNode.h"
#include "utility/Logging.h"
#include <imgui.h>

DebugForwardNode::DebugForwardNode(Scene& scene)
    : RenderGraphNode(ForwardRenderNode::name())
    , m_scene(scene)
{
}

RenderGraphNode::ExecuteCallback DebugForwardNode::constructFrame(Registry& reg) const
{
    const RenderTarget& windowTarget = reg.windowRenderTarget();

    // NOTE: We currently don't support multisampled window render targets, so for now this type of procedure works.

    Texture& colorTexture = reg.createTexture2D(windowTarget.extent(), Texture::Format::RGBA16F);
    reg.publish("color", colorTexture);

    Texture& depthTexture = reg.createMultisampledTexture2D(windowTarget.extent(), Texture::Format::Depth32F, multisamplingLevel());
    reg.publish("depth", depthTexture);

    RenderTarget* renderTarget;
    if (multisamplingLevel() == Texture::Multisampling::None) {
        renderTarget = &reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, &colorTexture },
                                                 { RenderTarget::AttachmentType::Depth, &depthTexture } });
    } else {
        Texture& msaaColorTexture = reg.createMultisampledTexture2D(windowTarget.extent(), Texture::Format::RGBA16F, multisamplingLevel());
        renderTarget = &reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, &msaaColorTexture, LoadOp::Clear, StoreOp::Store, &colorTexture },
                                                 { RenderTarget::AttachmentType::Depth, &depthTexture, LoadOp::Clear, StoreOp::Store } });
    }

    BindingSet& cameraBindingSet = *reg.getBindingSet("scene", "cameraSet");
    BindingSet& objectBindingSet = *reg.getBindingSet("scene", "objectSet");

    struct Vertex {
        vec3 position;
        vec2 texCoord;
        vec3 normal;
        vec4 tangent;
    };

    VertexLayout vertexLayout = VertexLayout {
        sizeof(Vertex),
        { { 0, VertexAttributeType::Float3, offsetof(Vertex, position) },
          { 1, VertexAttributeType::Float2, offsetof(Vertex, texCoord) },
          { 2, VertexAttributeType ::Float3, offsetof(Vertex, normal) },
          { 3, VertexAttributeType ::Float4, offsetof(Vertex, tangent) } }
    };

    Shader shader = Shader::createBasicRasterize("forward/debug.vert", "forward/debug.frag");

    RenderStateBuilder renderStateBuilder { *renderTarget, shader, vertexLayout };
    renderStateBuilder.polygonMode = PolygonMode::Filled;
    renderStateBuilder.addBindingSet(cameraBindingSet);
    renderStateBuilder.addBindingSet(objectBindingSet);
    RenderState& renderState = reg.createRenderState(renderStateBuilder);

    return [&](const AppState& appState, CommandList& cmdList) {
        m_scene.forEachMesh([](size_t, Mesh& mesh) {
            mesh.ensureIndexBuffer();
            mesh.ensureVertexBuffer({ VertexComponent::Position3F,
                                      VertexComponent::TexCoord2F,
                                      VertexComponent::Normal3F,
                                      VertexComponent::Tangent4F });
        });

        cmdList.beginRendering(renderState, ClearColor(0, 0, 0, 0), 1.0f);
        cmdList.bindSet(cameraBindingSet, 0);
        cmdList.bindSet(objectBindingSet, 1);

        m_scene.forEachMesh([&](size_t meshIndex, Mesh& mesh) {
            const Buffer& vertexBuffer = mesh.vertexBuffer({ VertexComponent::Position3F,
                                                             VertexComponent::TexCoord2F,
                                                             VertexComponent::Normal3F,
                                                             VertexComponent::Tangent4F });
            cmdList.drawIndexed(vertexBuffer, mesh.indexBuffer(), mesh.indexCount(), mesh.indexType(), meshIndex);
        });
    };
}
