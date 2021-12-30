#include "RTDiffuseGINode.h"

#include "ForwardRenderNode.h"
#include "LightData.h"
#include "RTAccelerationStructures.h"
#include <half.hpp>
#include <imgui.h>

RTDiffuseGINode::RTDiffuseGINode(Scene& scene)
    : RenderGraphNode(RTDiffuseGINode::name())
    , m_scene(scene)
{
}

std::string RTDiffuseGINode::name()
{
    return "rt-diffuse-gi";
}

void RTDiffuseGINode::constructNode(Registry& nodeReg)
{
    std::vector<Buffer*> vertexBuffers {};
    std::vector<Buffer*> indexBuffers {};
    std::vector<Texture*> allTextures {};
    std::vector<RTMesh> rtMeshes {};

    auto createTriangleMeshVertexBuffer = [&](Mesh& mesh) {
        // TODO: Would be nice if this could be cached too!
        std::vector<RTVertex> vertices {};
        {
            auto& posData = mesh.positionData();
            auto& normalData = mesh.normalData();
            auto& texCoordData = mesh.texcoordData();

            ASSERT(posData.size() == normalData.size());
            ASSERT(posData.size() == texCoordData.size());

            for (int i = 0; i < posData.size(); ++i) {
                vertices.push_back({ .position = vec4(posData[i], 0.0f),
                                     .normal = vec4(mesh.transform().localNormalMatrix() * normalData[i], 0.0f),
                                     .texCoord = vec4(texCoordData[i], 0.0f, 0.0f) });
            }
        }

        size_t texIndex = allTextures.size();
        allTextures.push_back(mesh.material().baseColorTexture());

        rtMeshes.push_back({ .objectId = (int)rtMeshes.size(),
                             .baseColor = (int)texIndex });

        // TODO: Later, we probably want to have combined vertex/ssbo and index/ssbo buffers instead!
        vertexBuffers.push_back(&nodeReg.createBuffer(vertices, Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal));
        indexBuffers.push_back(&nodeReg.createBuffer(mesh.indexData(), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal));
    };

    m_scene.forEachModel([&](size_t, Model& model) {
        model.forEachMesh([&](Mesh& mesh) {
            createTriangleMeshVertexBuffer(mesh);
        });
    });

    Buffer& meshBuffer = nodeReg.createBuffer(std::move(rtMeshes), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal);
    m_objectDataBindingSet = &nodeReg.createBindingSet({ { 0, ShaderStageRTClosestHit, &meshBuffer },
                                                         { 1, ShaderStageRTClosestHit, vertexBuffers },
                                                         { 2, ShaderStageRTClosestHit, indexBuffers },
                                                         { 3, ShaderStageRTClosestHit, allTextures, RT_MAX_TEXTURES } });

    m_accumulationTexture = &nodeReg.createTexture2D(m_scene.mainViewportSize(), Texture::Format::RGBA32F);
}

RenderGraphNode::ExecuteCallback RTDiffuseGINode::constructFrame(Registry& reg) const
{
    Texture* gBufferColor = reg.getTexture("g-buffer", "baseColor");
    Texture* gBufferNormal = reg.getTexture("g-buffer", "normal");
    Texture* gBufferDepth = reg.getTexture("g-buffer", "depth");

    Buffer& dirLightBuffer = reg.createBuffer(sizeof(DirectionalLightData), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);

    TopLevelAS& sceneTLAS = *reg.getTopLevelAccelerationStructure("rtAccStructureNodeScene");
    BindingSet& frameBindingSet = reg.createBindingSet({ { 0, ShaderStage(ShaderStageRTRayGen | ShaderStageRTClosestHit), &sceneTLAS },
                                                         { 1, ShaderStageRTRayGen, m_accumulationTexture, ShaderBindingType::StorageImage },
                                                         { 2, ShaderStageRTRayGen, gBufferColor, ShaderBindingType::TextureSampler },
                                                         { 3, ShaderStageRTRayGen, gBufferNormal, ShaderBindingType::TextureSampler },
                                                         { 4, ShaderStageRTRayGen, gBufferDepth, ShaderBindingType::TextureSampler },
                                                         { 5, ShaderStageRTRayGen, reg.getBuffer("scene", "camera") },
                                                         { 6, ShaderStageRTMiss, reg.getBuffer("scene", "environmentData") },
                                                         { 7, ShaderStageRTMiss, reg.getTexture("scene", "environmentMap"), ShaderBindingType::TextureSampler },
                                                         { 8, ShaderStageRTClosestHit, &dirLightBuffer } });

    ShaderFile raygen = ShaderFile("rt-diffuseGI/raygen.rgen");
    HitGroup mainHitGroup { ShaderFile("rt-diffuseGI/closestHit.rchit") };
    std::vector<ShaderFile> missShaders { ShaderFile("rt-diffuseGI/miss.rmiss"),
                                          ShaderFile("rt-diffuseGI/shadow.rmiss") };
    ShaderBindingTable sbt { raygen, { mainHitGroup }, missShaders };

    StateBindings stateDataBindings;
    stateDataBindings.at(0, frameBindingSet);
    stateDataBindings.at(1, *m_objectDataBindingSet);

    uint32_t maxRecursionDepth = 2;
    RayTracingState& rtState = reg.createRayTracingState(sbt, stateDataBindings, maxRecursionDepth);

    Texture& diffuseGI = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA16F);
    reg.publish("diffuseGI", diffuseGI);

    BindingSet& avgAccumBindingSet = reg.createBindingSet({ { 0, ShaderStageCompute, m_accumulationTexture, ShaderBindingType::StorageImage },
                                                            { 1, ShaderStageCompute, &diffuseGI, ShaderBindingType::StorageImage } });
    ComputeState& compAvgAccumState = reg.createComputeState(Shader::createCompute("rt-diffuseGI/averageAccum.comp"), { &avgAccumBindingSet });

    return [&](const AppState& appState, CommandList& cmdList) {
        constexpr int samplesPerPass = 1; // (I don't wanna pass in a uniform for optimization reasons, so keep this up to date!)
        int currentSamplesPerPixel = samplesPerPass * m_numAccumulatedFrames;

        if (currentSamplesPerPixel < maxSamplesPerPixel) {
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "Accumulating ... (%i SPP)", currentSamplesPerPixel);
        } else {
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "Ready! (%i SPP)", currentSamplesPerPixel);
        }

        static bool doRender = true;
        ImGui::Checkbox("Render", &doRender);
        static bool ignoreColor = false;
        ImGui::Checkbox("Ignore color", &ignoreColor);

        if (!doRender)
            return;

        const DirectionalLight& light = m_scene.sun();
        DirectionalLightData dirLightData {
            .color = light.color * light.intensityValue() * m_scene.lightPreExposureValue(),
            .exposure = m_scene.lightPreExposureValue(),
            .worldSpaceDirection = vec4(normalize(light.direction), 0.0),
            .viewSpaceDirection = m_scene.camera().viewMatrix() * vec4(normalize(m_scene.sun().direction), 0.0),
            .lightProjectionFromWorld = light.viewProjection(),
            .lightProjectionFromView = light.viewProjection() * inverse(m_scene.camera().viewMatrix())
        };
        dirLightBuffer.updateData(&dirLightData, sizeof(DirectionalLightData));

        cmdList.setRayTracingState(rtState);

        cmdList.pushConstant(ShaderStageRTRayGen, ignoreColor);
        cmdList.pushConstant(ShaderStageRTRayGen, appState.frameIndex(), 4);

        cmdList.waitEvent(0, appState.frameIndex() == 0 ? PipelineStage::Host : PipelineStage::RayTracing);
        cmdList.resetEvent(0, PipelineStage::RayTracing);
        {
            if (m_scene.camera().didModify() || Input::instance().isKeyDown(Key::R)) {
                cmdList.clearTexture(*m_accumulationTexture, ClearColor::srgbColor(0, 0, 0));
                m_numAccumulatedFrames = 0;
            }

            if (currentSamplesPerPixel < maxSamplesPerPixel) {
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
        cmdList.signalEvent(0, PipelineStage::RayTracing);
    };
}
