#include "TonemapNode.h"

#include "SceneNode.h"
#include "utility/Logging.h"
#include "utility/Profiling.h"
#include <imgui.h>

TonemapNode::TonemapNode(Scene& scene, std::string sourceTextureName, Mode mode)
    : m_scene(scene)
    , m_sourceTextureName(sourceTextureName)
    , m_mode(mode)
{
}

RenderPipelineNode::ExecuteCallback TonemapNode::constructFrame(Registry& reg) const
{
    Texture* sourceTexture = reg.getTexture(m_sourceTextureName);
    if (!sourceTexture)
        LogErrorAndExit("Tonemap: specified source texture '%s' not found, exiting.\n", m_sourceTextureName.c_str());

    const RenderTarget* ldrTarget;
    if (m_mode == Mode::RenderToWindow) {
        ldrTarget = &reg.windowRenderTarget();
    } else {
        Texture& ldrTexture = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA8);
        reg.publish("SceneColorLDR", ldrTexture);
        ldrTarget = &reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, &ldrTexture, LoadOp::Discard } });
    }

    // TODO: We should probably use compute for this.. we don't require interpolation or any type of depth writing etc.
    std::vector<vec2> fullScreenTriangle { { -1, -3 }, { -1, 1 }, { 3, 1 } };
    Buffer& vertexBuffer = reg.createBuffer(std::move(fullScreenTriangle), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal);
    VertexLayout vertexLayout = VertexLayout { VertexComponent::Position2F };

    BindingSet& tonemapBindingSet = reg.createBindingSet({ { 0, ShaderStageFragment, sourceTexture, ShaderBindingType::TextureSampler } });
    Shader tonemapShader = Shader::createBasicRasterize("tonemap/tonemap.vert", "tonemap/tonemap.frag");
    RenderStateBuilder tonemapStateBuilder { *ldrTarget, tonemapShader, vertexLayout };
    tonemapStateBuilder.stateBindings().at(0, tonemapBindingSet);
    tonemapStateBuilder.writeDepth = false;
    tonemapStateBuilder.testDepth = false;
    RenderState& tonemapRenderState = reg.createRenderState(tonemapStateBuilder);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        if (m_mode == Mode::RenderToWindow)
            cmdList.beginRendering(tonemapRenderState, ClearColor::black(), 1.0f);
        else
            cmdList.beginRendering(tonemapRenderState);

        cmdList.draw(vertexBuffer, 3);

        cmdList.endRendering();
    };
}
