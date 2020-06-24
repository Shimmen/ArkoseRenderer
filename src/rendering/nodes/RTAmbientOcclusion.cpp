#include "RTAmbientOcclusion.h"

#include "ForwardRenderNode.h"
#include "RTAccelerationStructures.h"
#include "SceneUniformNode.h"
#include "utility/GlobalState.h"
#include <imgui.h>

RTAmbientOcclusion::RTAmbientOcclusion(const Scene& scene)
    : RenderGraphNode(RTAmbientOcclusion::name())
    , m_scene(scene)
{
}

std::string RTAmbientOcclusion::name()
{
    return "rt-ambient-occlusion";
}

void RTAmbientOcclusion::constructNode(Registry& reg)
{
    Extent2D windowExtent = GlobalState::get().windowExtent();
    m_accumulatedAO = &reg.createTexture2D(windowExtent, Texture::Format::R16F, Texture::Usage::StorageAndSample);
}

RenderGraphNode::ExecuteCallback RTAmbientOcclusion::constructFrame(Registry& reg) const
{
    const Texture* gBufferNormal = reg.getTexture(ForwardRenderNode::name(), "normal").value();
    const Texture* gBufferDepth = reg.getTexture(ForwardRenderNode::name(), "depth").value();

    Texture& ambientOcclusion = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::R16F, Texture::Usage::StorageAndSample);
    reg.publish("AO", ambientOcclusion);

    const TopLevelAS& tlas = *reg.getTopLevelAccelerationStructure(RTAccelerationStructures::name(), "scene");
    BindingSet& frameBindingSet = reg.createBindingSet({ { 0, ShaderStageRTRayGen, &tlas },
                                                         { 1, ShaderStageRTRayGen, reg.getBuffer(SceneUniformNode::name(), "camera") },
                                                         { 2, ShaderStageRTRayGen, m_accumulatedAO, ShaderBindingType::StorageImage },
                                                         { 3, ShaderStageRTRayGen, gBufferNormal, ShaderBindingType::TextureSampler },
                                                         { 4, ShaderStageRTRayGen, gBufferDepth, ShaderBindingType::TextureSampler } });

    ShaderFile raygen("rt-ao/raygen.rgen");
    ShaderFile miss("rt-ao/miss.rmiss");
    HitGroup triangleHitGroup(ShaderFile("rt-ao/closestHit.rchit"));
    ShaderBindingTable sbt { raygen, { triangleHitGroup }, { miss } };

    uint32_t maxRecursionDepth = 1;
    RayTracingState& rtState = reg.createRayTracingState(sbt, { &frameBindingSet }, maxRecursionDepth);

    BindingSet& avgAccumBindingSet = reg.createBindingSet({ { 0, ShaderStageCompute, m_accumulatedAO, ShaderBindingType::StorageImage },
                                                            { 1, ShaderStageCompute, &ambientOcclusion, ShaderBindingType::StorageImage } });
    ComputeState& compAvgAccumState = reg.createComputeState(Shader::createCompute("common/averageAccum.comp"), { &avgAccumBindingSet });

    return [&](const AppState& appState, CommandList& cmdList) {
        static bool enabled = false;
        ImGui::Checkbox("Enabled", &enabled);
        static int signedNumSamples = 1;
        ImGui::SliderInt("Sample count", &signedNumSamples, 1, 32);
        static float radius = 0.13f;
        ImGui::SliderFloat("Max radius", &radius, 0.01f, 0.5f);
        static float darkening = 20.0f;
        ImGui::SliderFloat("Darkening", &darkening, 1.0f, 40.0f);

        if (Input::instance().wasKeyPressed(GLFW_KEY_O)) {
            enabled = false;
        }
        if (Input::instance().wasKeyPressed(GLFW_KEY_P)) {
            enabled = true;
        }

        if (!enabled) {
            cmdList.clearTexture(ambientOcclusion, ClearColor(1, 1, 1));
            return;
        }

        cmdList.waitEvent(1, appState.frameIndex() == 0 ? PipelineStage::Host : PipelineStage::RayTracing);
        cmdList.resetEvent(1, PipelineStage::RayTracing);
        {
            if (m_scene.camera().didModify() || Input::instance().isKeyDown(GLFW_KEY_R)) {
                cmdList.clearTexture(*m_accumulatedAO, ClearColor(0, 0, 0));
                m_numAccumulatedFrames = 0;
            }

            if (m_numAccumulatedFrames < 256) {
                cmdList.setRayTracingState(rtState);
                cmdList.bindSet(frameBindingSet, 0);
                cmdList.pushConstant(ShaderStageRTRayGen, radius, 0);
                cmdList.pushConstant(ShaderStageRTRayGen, static_cast<uint32_t>(signedNumSamples), 4);
                cmdList.pushConstant(ShaderStageRTRayGen, appState.frameIndex(), 8);
                cmdList.pushConstant(ShaderStageRTRayGen, (uint32_t)RTAccelerationStructures::HitMask::TriangleMeshWithProxy, 12);
                cmdList.pushConstant(ShaderStageRTRayGen, darkening, 16);
                cmdList.traceRays(appState.windowExtent());
                m_numAccumulatedFrames += 1;
            }

            cmdList.debugBarrier(); // TODO: Add fine grained barrier here to make sure ray tracing is done before averaging!

            cmdList.setComputeState(compAvgAccumState);
            cmdList.bindSet(avgAccumBindingSet, 0);
            cmdList.pushConstant(ShaderStageCompute, m_numAccumulatedFrames);

            Extent2D globalSize = appState.windowExtent();
            cmdList.dispatch(globalSize, Extent3D(16));
        }
        cmdList.signalEvent(1, PipelineStage::RayTracing);
    };
}
