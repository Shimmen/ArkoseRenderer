#include "FinalPostFxNode.h"

#include "ForwardRenderNode.h"
#include "RTAmbientOcclusion.h"
#include "RTDiffuseGINode.h"
#include "RTFirstHitNode.h"
#include "RTReflectionsNode.h"
#include "SceneNode.h"
#include "imgui.h"

FinalPostFxNode::FinalPostFxNode(const Scene& scene)
    : RenderGraphNode(FinalPostFxNode::name())
    , m_scene(scene)
{
}

std::string FinalPostFxNode::name()
{
    return "final";
}

FinalPostFxNode::ExecuteCallback FinalPostFxNode::constructFrame(Registry& reg) const
{
    Shader shader = Shader::createBasicRasterize("final/finalPostFx.vert", "final/finalPostFx.frag");

    VertexLayout vertexLayout = VertexLayout { sizeof(vec2), { { 0, VertexAttributeType::Float2, 0 } } };
    std::vector<vec2> fullScreenTriangle { { -1, -3 }, { -1, 1 }, { 3, 1 } };
    Buffer& vertexBuffer = reg.createBuffer(std::move(fullScreenTriangle), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal);

    // TODO: Consider placing something like this in the Registry itself so we can just do value_or(reg.placeholderTexture())
    Texture& placeholder = reg.loadTexture2D("assets/test-pattern.png", true, true);

    const Texture* sourceTexture = reg.getTexture(ForwardRenderNode::name(), "color").value_or(&placeholder);
    const Texture* sourceTextureRt = reg.getTexture(RTFirstHitNode::name(), "image").value_or(&placeholder);

    BindingSet& sourceImage = reg.createBindingSet({ { 0, ShaderStageFragment, sourceTexture, ShaderBindingType::TextureSampler } });
    BindingSet& sourceImageRt = reg.createBindingSet({ { 0, ShaderStageFragment, sourceTextureRt, ShaderBindingType::TextureSampler } });

    // TODO: Maybe return an optional instead, so we can use value_or(..)
    const Texture* diffuseGI = reg.getTexture(RTDiffuseGINode::name(), "diffuseGI").value_or(&reg.createPixelTexture(vec4(0, 0, 0, 1), true));
    const Texture* ambientOcclusion = reg.getTexture(RTAmbientOcclusion::name(), "AO").value_or(&reg.createPixelTexture(vec4(1, 1, 1, 1), true));
    BindingSet& etcBindingSet = reg.createBindingSet({ { 0, ShaderStageFragment, diffuseGI, ShaderBindingType::TextureSampler },
                                                       { 1, ShaderStageFragment, ambientOcclusion, ShaderBindingType::TextureSampler } });

    BindingSet& envBindingSet = reg.createBindingSet({ { 0, ShaderStageVertex, reg.getBuffer("scene", "camera") },
                                                       { 1, ShaderStageFragment, reg.getTexture("scene", "environmentMap").value_or(&reg.createPixelTexture(vec4(1), true)), ShaderBindingType::TextureSampler },
                                                       { 2, ShaderStageFragment, reg.getTexture("g-buffer", "depth").value(), ShaderBindingType::TextureSampler },
                                                       { 3, ShaderStageFragment, reg.getBuffer("scene", "environmentData") } });

    RenderStateBuilder renderStateBuilder { reg.windowRenderTarget(), shader, vertexLayout };
    renderStateBuilder.addBindingSet(sourceImage).addBindingSet(sourceImageRt).addBindingSet(etcBindingSet).addBindingSet(envBindingSet);
    renderStateBuilder.writeDepth = false;
    renderStateBuilder.testDepth = false;

    RenderState& renderState = reg.createRenderState(renderStateBuilder);

    return [&](const AppState& appState, CommandList& cmdList) {
        static bool useRtFirstHit = false;
        ImGui::Checkbox("Use ray traced first-hit", &useRtFirstHit);
        static bool includeDiffuseGI = true;
        ImGui::Checkbox("Include diffuse GI", &includeDiffuseGI);
        static float exposure = 0.45f;
        ImGui::SliderFloat("Exposure", &exposure, 0.01f, 10.0f, "%.3f", 3.0f);

        cmdList.beginRendering(renderState, ClearColor(0.5f, 0.1f, 0.5f), 1.0f);
        cmdList.bindSet(useRtFirstHit ? sourceImageRt : sourceImage, 0);
        cmdList.bindSet(etcBindingSet, 1);
        cmdList.bindSet(envBindingSet, 2);

        cmdList.pushConstant(ShaderStageFragment, includeDiffuseGI);
        cmdList.pushConstant(ShaderStageFragment, exposure, 4);

        cmdList.draw(vertexBuffer, 3);

        if (ImGui::Button("Take screenshot")) {
            static int imageIdx = 0;
            const Texture& finalColor = *reg.windowRenderTarget().attachment(RenderTarget::AttachmentType::Color0);
            cmdList.saveTextureToFile(finalColor, "assets/screenshot_" + std::to_string(imageIdx++) + ".png");
        }
    };
}
