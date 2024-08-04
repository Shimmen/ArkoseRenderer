#include "TonemapNode.h"

#include "core/Logging.h"
#include "rendering/RenderPipeline.h"
#include <imgui.h>

#include "shaders/shared/TonemapData.h"

TonemapNode::TonemapNode(std::string sourceTextureName, Mode mode)
    : m_sourceTextureName(sourceTextureName)
    , m_mode(mode)
    , m_tonemapMethod(TONEMAP_METHOD_AGX)
{
}

void TonemapNode::drawGui()
{
    ImGui::Text("Method:");

    if (ImGui::RadioButton("Clamp", m_tonemapMethod == TONEMAP_METHOD_CLAMP)) {
        m_tonemapMethod = TONEMAP_METHOD_CLAMP;
    }
    if (ImGui::RadioButton("Reinhard", m_tonemapMethod == TONEMAP_METHOD_REINHARD)) {
        m_tonemapMethod = TONEMAP_METHOD_REINHARD;
    }
    if (ImGui::RadioButton("ACES", m_tonemapMethod == TONEMAP_METHOD_ACES)) {
        m_tonemapMethod = TONEMAP_METHOD_ACES;
    }
    if (ImGui::RadioButton("AgX", m_tonemapMethod == TONEMAP_METHOD_AGX)) {
        m_tonemapMethod = TONEMAP_METHOD_AGX;
    }
}

RenderPipelineNode::ExecuteCallback TonemapNode::construct(GpuScene& scene, Registry& reg)
{
    Texture* sourceTexture = reg.getTexture(m_sourceTextureName);
    if (!sourceTexture) {
        ARKOSE_LOG(Fatal, "Tonemap: specified source texture '{}' not found, exiting.\n", m_sourceTextureName);
    }

    const RenderTarget* ldrTarget;
    if (m_mode == Mode::RenderToWindow) {
        ldrTarget = &reg.windowRenderTarget();
    } else {
        Texture& ldrTexture = reg.createTexture2D(sourceTexture->extent(), Texture::Format::RGBA8);
        reg.publish("SceneColorLDR", ldrTexture);
        ldrTarget = &reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, &ldrTexture, LoadOp::Discard } });
    }

    // TODO: We should probably use compute for this.. we don't require interpolation or any type of depth writing etc.
    std::vector<vec2> fullScreenTriangle { { -1, -3 }, { -1, 1 }, { 3, 1 } };
    Buffer& vertexBuffer = reg.createBuffer(std::move(fullScreenTriangle), Buffer::Usage::Vertex);
    VertexLayout vertexLayout = VertexLayout { VertexComponent::Position2F };

    BindingSet& tonemapBindingSet = reg.createBindingSet({ ShaderBinding::sampledTexture(*sourceTexture, ShaderStage::Fragment) });
    Shader tonemapShader = Shader::createBasicRasterize("tonemap/tonemap.vert", "tonemap/tonemap.frag");
    RenderStateBuilder tonemapStateBuilder { *ldrTarget, tonemapShader, vertexLayout };
    tonemapStateBuilder.stateBindings().at(0, tonemapBindingSet);
    tonemapStateBuilder.writeDepth = false;
    tonemapStateBuilder.testDepth = false;
    RenderState& tonemapRenderState = reg.createRenderState(tonemapStateBuilder);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        if (m_mode == Mode::RenderToWindow) {
            cmdList.beginRendering(tonemapRenderState, ClearValue::blackAtMaxDepth());
        } else {
            cmdList.beginRendering(tonemapRenderState);
        }

        cmdList.setNamedUniform<int>("tonemapMethod", m_tonemapMethod);

        cmdList.bindVertexBuffer(vertexBuffer, tonemapRenderState.vertexLayout().packedVertexSize(), 0);
        cmdList.draw(3);

        cmdList.endRendering();
    };
}
