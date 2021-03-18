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

    Shader shader = Shader::createBasicRasterize("forward/debug.vert", "forward/debug.frag");

    RenderStateBuilder renderStateBuilder { *renderTarget, shader, m_vertexLayout };
    renderStateBuilder.polygonMode = PolygonMode::Filled;
    renderStateBuilder.addBindingSet(cameraBindingSet);
    renderStateBuilder.addBindingSet(objectBindingSet);
    RenderState& renderState = reg.createRenderState(renderStateBuilder);

    return [&](const AppState& appState, CommandList& cmdList) {
        m_scene.forEachMesh([&](size_t, Mesh& mesh) {
            mesh.ensureDrawCallIsReady(m_vertexLayout, m_scene);
        });

        cmdList.beginRendering(renderState, ClearColor(0, 0, 0, 0), 1.0f);
        cmdList.bindSet(cameraBindingSet, 0);
        cmdList.bindSet(objectBindingSet, 1);

        m_scene.forEachMesh([&](size_t meshIndex, Mesh& mesh) {
            DrawCallDescription drawCall = mesh.drawCallDescription(m_vertexLayout, m_scene);
            drawCall.firstInstance = meshIndex; // TODO: Put this in some buffer instead!
            cmdList.issueDrawCall(drawCall);
        });
    };
}
