#include "FinalNode.h"

#include "core/Logging.h"
#include "utility/Profiling.h"
#include <imgui.h>

FinalNode::FinalNode(std::string sourceTextureName)
    : m_sourceTextureName(std::move(sourceTextureName))
{
}

RenderPipelineNode::ExecuteCallback FinalNode::construct(GpuScene& scene, Registry& reg)
{
    Texture* sourceTexture = reg.getTexture(m_sourceTextureName);
    if (!sourceTexture)
        ARKOSE_LOG(Fatal, "Final: specified source texture '{}' not found, exiting.", m_sourceTextureName);

    Texture& filmGrainTexture = reg.loadTextureArrayFromFileSequence("assets/blue-noise/64_64/HDR_RGBA_{}.png", false, false);

    BindingSet& bindingSet = reg.createBindingSet({ ShaderBinding::sampledTexture(*sourceTexture, ShaderStage::Fragment),
                                                    ShaderBinding::sampledTexture(filmGrainTexture, ShaderStage::Fragment) });

    std::vector<vec2> fullScreenTriangle { { -1, -3 }, { -1, 1 }, { 3, 1 } };
    Buffer& vertexBuffer = reg.createBuffer(std::move(fullScreenTriangle), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal);

    Shader taaShader = Shader::createBasicRasterize("final/final.vert", "final/postprocessing.frag");
    RenderStateBuilder stateBuilder { reg.windowRenderTarget(), taaShader, VertexLayout { VertexComponent::Position2F } };
    stateBuilder.stateBindings().at(0, bindingSet);
    stateBuilder.writeDepth = false;
    stateBuilder.testDepth = false;
    RenderState& renderState = reg.createRenderState(stateBuilder);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        static bool addFilmGrain = true;
        ImGui::Checkbox("Add film grain", &addFilmGrain);
        float filmGrainGain = addFilmGrain ? scene.scene().filmGrainGain() : 0.0f;

        static float filmGrainScale = 2.5f;
        ImGui::SliderFloat("Film grain scale", &filmGrainScale, 1.0f, 10.0f);

        cmdList.beginRendering(renderState, ClearColor::black(), 1.0f);
        {
            cmdList.setNamedUniform("filmGrainGain", filmGrainGain);
            cmdList.setNamedUniform("filmGrainScale", filmGrainScale);
            cmdList.setNamedUniform("filmGrainArrayIdx", appState.frameIndex() % filmGrainTexture.arrayCount());
        }
        cmdList.draw(vertexBuffer, 3);
        cmdList.endRendering();
    };
}
