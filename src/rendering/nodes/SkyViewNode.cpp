#include "SkyViewNode.h"

#include "utility/Profiling.h"
#include <imgui.h>

SkyViewNode::SkyViewNode(Scene& scene)
    : RenderGraphNode(SkyViewNode::name())
    , m_scene(scene)
{
}

RenderGraphNode::ExecuteCallback SkyViewNode::constructFrame(Registry& reg) const
{
    SCOPED_PROFILE_ZONE();

    Texture& targetImage = *reg.getTexture("forward", "color").value();
    Texture& depthStencilImage = *reg.getTexture("g-buffer", "depth").value();

    Texture& skyViewTexture = m_scene.environmentMap().empty()
        ? reg.createPixelTexture(vec4(1.0f), true)
        : reg.loadTexture2D(m_scene.environmentMap(), true, false);

    BindingSet& skyViewComputeBindingSet = reg.createBindingSet({ { 0, ShaderStageCompute, reg.getBuffer("scene", "camera") },
                                                                  { 1, ShaderStageCompute, &targetImage, ShaderBindingType::StorageImage },
                                                                  { 2, ShaderStageCompute, &depthStencilImage, ShaderBindingType::TextureSampler },
                                                                  { 3, ShaderStageCompute, &skyViewTexture, ShaderBindingType::TextureSampler } });
    ComputeState& skyViewComputeState = reg.createComputeState(Shader::createCompute("sky-view/sky-view.comp"), { &skyViewComputeBindingSet });

    BindingSet& skyViewRasterizeBindingSet = reg.createBindingSet({ { 0, ShaderStageVertex, reg.getBuffer("scene", "camera") },
                                                                    { 1, ShaderStageFragment, &skyViewTexture, ShaderBindingType::TextureSampler } });
    RenderTarget& renderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, &targetImage, LoadOp::Load, StoreOp::Store },
                                                          { RenderTarget::AttachmentType::Depth, &depthStencilImage, LoadOp::Load, StoreOp::Store } });
    Shader rasterizeShader = Shader::createBasicRasterize("sky-view/sky-view.vert", "sky-view/sky-view.frag");
    RenderStateBuilder renderStateBuilder { renderTarget, rasterizeShader, { VertexComponent::Position2F } };
    renderStateBuilder.testDepth = true;
    renderStateBuilder.writeDepth = false;
    renderStateBuilder.stencilMode = StencilMode::PassIfZero; // i.e. if no geometry is written to this pixel
    renderStateBuilder.stateBindings().at(0, skyViewRasterizeBindingSet);
    RenderState& skyViewRenderState = reg.createRenderState(renderStateBuilder);
    Buffer& fullscreenTriangleVertexBuffer = reg.createBuffer(std::vector<vec2> { { -1, -3 }, { -1, 1 }, { 3, 1 } },
                                                              Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal);

    return [&](const AppState& appState, CommandList& cmdList) {

        static bool enabled = true;
        ImGui::Checkbox("Enabled##SkyViewNode", &enabled);

        static bool useRasterizedPath = true;
        ImGui::Checkbox("Use rasterized path", &useRasterizedPath);

        // NOTE: Obviously the unit of this is dependent on the values in the texture.
        ImGui::SliderFloat("Environment multiplier", &m_scene.environmentMultiplier(), 1000.0f, 15000.0f);
        float envMultiplier = m_scene.exposedEnvironmentMultiplier();

        if (!enabled)
            return;

        if (useRasterizedPath) {
            // Uses the stencil buffer to old process pixels with no geometry (faster, but must be done in-line)
            cmdList.beginRendering(skyViewRenderState);
            cmdList.setNamedUniform("environmentMultiplier", envMultiplier);
            cmdList.draw(fullscreenTriangleVertexBuffer, 3);
            cmdList.endRendering();
        } else {
            // Uses the depth buffer to figure out if we should write (slower, but could in theory be done in parallel)
            cmdList.setComputeState(skyViewComputeState);
            cmdList.bindSet(skyViewComputeBindingSet, 0);
            cmdList.setNamedUniform("environmentMultiplier", envMultiplier);
            cmdList.dispatch(targetImage.extent(), { 16, 16, 1 });
        }
    };
}
