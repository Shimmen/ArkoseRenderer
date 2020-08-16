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

void DebugForwardNode::constructNode(Registry& nodeReg)
{
    m_drawables.clear();
    m_scene.forEachMesh([&](size_t, Mesh& mesh) {
        Drawable drawable {};
        drawable.mesh = &mesh;

        drawable.objectDataBuffer = &nodeReg.createBuffer(sizeof(PerForwardObject), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);
        drawable.bindingSet = &nodeReg.createBindingSet(
            { { 0, ShaderStageVertex, drawable.objectDataBuffer },
              { 1, ShaderStageFragment, mesh.material().baseColorTexture(), ShaderBindingType::TextureSampler } });

        m_drawables.push_back(drawable);
    });
}

RenderGraphNode::ExecuteCallback DebugForwardNode::constructFrame(Registry& reg) const
{
    const RenderTarget& windowTarget = reg.windowRenderTarget();

    // NOTE: We currently don't support multisampled window render targets, so for now this type of procedure works.

    Texture& colorTexture = reg.createTexture2D(windowTarget.extent(), Texture::Format::RGBA16F);
    reg.publish("color", colorTexture);

    Texture& depthTexture = reg.createTexture2D(windowTarget.extent(), Texture::Format::Depth32F, Texture::Mipmap::None, multisamplingLevel());
    reg.publish("depth", depthTexture);

    RenderTarget* renderTarget;
    if (multisamplingLevel() == Texture::Multisampling::None) {
        renderTarget = &reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, &colorTexture },
                                                 { RenderTarget::AttachmentType::Depth, &depthTexture } });
    } else {
        Texture& msaaColorTexture = reg.createTexture2D(windowTarget.extent(), Texture::Format::RGBA16F, Texture::Mipmap::None, multisamplingLevel());
        renderTarget = &reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, &msaaColorTexture, LoadOp::Clear, StoreOp::Store, &colorTexture },
                                                 { RenderTarget::AttachmentType::Depth, &depthTexture, LoadOp::Clear, StoreOp::Store } });
    }

    Buffer* cameraUniformBuffer = reg.getBuffer("scene", "camera");
    BindingSet& fixedBindingSet = reg.createBindingSet({ { 0, ShaderStage(ShaderStageVertex | ShaderStageFragment), cameraUniformBuffer } });

    VertexLayout vertexLayout = VertexLayout {
        sizeof(ForwardVertex),
        { { 0, VertexAttributeType::Float3, offsetof(ForwardVertex, position) },
          { 1, VertexAttributeType::Float2, offsetof(ForwardVertex, texCoord) },
          { 2, VertexAttributeType ::Float3, offsetof(ForwardVertex, normal) },
          { 3, VertexAttributeType ::Float4, offsetof(ForwardVertex, tangent) } }
    };

    Shader shader = Shader::createBasicRasterize("forward/debug.vert", "forward/debug.frag");

    RenderStateBuilder renderStateBuilder { *renderTarget, shader, vertexLayout };
    renderStateBuilder.polygonMode = PolygonMode::Filled;

    renderStateBuilder.addBindingSet(fixedBindingSet);
    for (auto& drawable : m_drawables)
        renderStateBuilder.addBindingSet(*drawable.bindingSet);
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
        cmdList.bindSet(fixedBindingSet, 0);

        for (const Drawable& drawable : m_drawables) {

            PerForwardObject objectData {
                .worldFromLocal = drawable.mesh->transform().worldMatrix(),
                .worldFromTangent = mat4(drawable.mesh->transform().worldNormalMatrix())
            };
            drawable.objectDataBuffer->updateData(&objectData, sizeof(PerForwardObject));

            cmdList.bindSet(*drawable.bindingSet, 1);

            const Buffer& indexBuffer = drawable.mesh->indexBuffer();
            const Buffer& vertexBuffer = drawable.mesh->vertexBuffer({ VertexComponent::Position3F,
                                                                       VertexComponent::TexCoord2F,
                                                                       VertexComponent::Normal3F,
                                                                       VertexComponent::Tangent4F });
            cmdList.drawIndexed(vertexBuffer, indexBuffer, drawable.mesh->indexCount(), drawable.mesh->indexType());
        }
    };
}
