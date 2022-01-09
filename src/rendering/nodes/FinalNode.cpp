#include "FinalNode.h"

#include "SceneNode.h"
#include "utility/Logging.h"
#include "utility/Profiling.h"
#include <imgui.h>

FinalNode::FinalNode(Scene& scene, std::string sourceTextureName)
    : m_scene(scene)
    , m_sourceTextureName(std::move(sourceTextureName))
{
}

RenderPipelineNode::ExecuteCallback FinalNode::constructFrame(Registry& reg) const
{
    SCOPED_PROFILE_ZONE();

    Texture* sourceTexture = reg.getTexture(m_sourceTextureName);
    if (!sourceTexture)
        LogErrorAndExit("Final: specified source texture '%s' not found, exiting.\n", m_sourceTextureName.c_str());

    BindingSet& bindingSet = reg.createBindingSet({ { 0, ShaderStageFragment, sourceTexture, ShaderBindingType::TextureSampler } });

    std::vector<vec2> fullScreenTriangle { { -1, -3 }, { -1, 1 }, { 3, 1 } };
    Buffer& vertexBuffer = reg.createBuffer(std::move(fullScreenTriangle), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal);

    Shader taaShader = Shader::createBasicRasterize("final/final.vert", "final/with-filmgrain.frag");
    RenderStateBuilder stateBuilder { reg.windowRenderTarget(), taaShader, VertexLayout { VertexComponent::Position2F } };
    stateBuilder.stateBindings().at(0, bindingSet);
    stateBuilder.writeDepth = false;
    stateBuilder.testDepth = false;
    RenderState& renderState = reg.createRenderState(stateBuilder);

    return [&](const AppState& appState, CommandList& cmdList) {

        static bool addFilmGrain = true;
        ImGui::Checkbox("Add film grain", &addFilmGrain);
        float filmGrainGain = addFilmGrain ? m_scene.filmGrainGain() : 0.0f;

        cmdList.beginRendering(renderState, ClearColor::black(), 1.0f);
        {
            cmdList.setNamedUniform("filmGrainGain", filmGrainGain);
            cmdList.setNamedUniform("frameIndex", appState.frameIndex());
        }
        cmdList.draw(vertexBuffer, 3);
        cmdList.endRendering();
    };
}
