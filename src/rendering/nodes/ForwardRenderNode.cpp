#include "ForwardRenderNode.h"

#include "LightData.h"
#include "SceneNode.h"
#include "utility/Logging.h"

std::string ForwardRenderNode::name()
{
    return "forward";
}

ForwardRenderNode::ForwardRenderNode(Scene& scene)
    : RenderGraphNode(ForwardRenderNode::name())
    , m_scene(scene)
{
}

RenderGraphNode::ExecuteCallback ForwardRenderNode::constructFrame(Registry& reg) const
{
    Texture& colorTexture = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA16F);
    reg.publish("color", colorTexture);

    RenderTarget& renderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, &colorTexture },
                                                          { RenderTarget::AttachmentType::Color1, reg.getTexture("g-buffer", "normal").value() },
                                                          { RenderTarget::AttachmentType::Color2, reg.getTexture("g-buffer", "baseColor").value() },
                                                          { RenderTarget::AttachmentType::Depth, reg.getTexture("g-buffer", "depth").value() } });

    BindingSet& cameraBindingSet = *reg.getBindingSet("scene", "cameraSet");
    BindingSet& objectBindingSet = *reg.getBindingSet("scene", "objectSet");
    BindingSet& lightBindingSet = *reg.getBindingSet("scene", "lightSet");

    Shader shader = Shader::createBasicRasterize("forward/forward.vert", "forward/forward.frag");
    RenderStateBuilder renderStateBuilder { renderTarget, shader, vertexLayout };
    renderStateBuilder.addBindingSet(cameraBindingSet);
    renderStateBuilder.addBindingSet(objectBindingSet);
    renderStateBuilder.addBindingSet(lightBindingSet);
    RenderState& renderState = reg.createRenderState(renderStateBuilder);

    m_scene.forEachMesh([&](size_t, Mesh& mesh) {
        mesh.ensureVertexBuffer(semanticVertexLayout);
        mesh.ensureIndexBuffer();
    });

    return [&](const AppState& appState, CommandList& cmdList) {
        cmdList.beginRendering(renderState, ClearColor(0, 0, 0, 0), 1.0f);
        cmdList.pushConstant(ShaderStageFragment, m_scene.ambient(), 0);

        cmdList.bindSet(cameraBindingSet, 0);
        cmdList.bindSet(objectBindingSet, 1);
        cmdList.bindSet(lightBindingSet, 2);

        m_scene.forEachMesh([&](size_t meshIndex, Mesh& mesh) {
            cmdList.drawIndexed(mesh.vertexBuffer(semanticVertexLayout),
                                mesh.indexBuffer(), mesh.indexCount(), mesh.indexType(),
                                meshIndex);
        });
    };
}
