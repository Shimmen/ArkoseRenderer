#include "RTReflectionsNode.h"

#include "ForwardRenderNode.h"
#include "LightData.h"
#include "RTAccelerationStructures.h"

RTReflectionsNode::RTReflectionsNode(Scene& scene)
    : RenderGraphNode(RTReflectionsNode::name())
    , m_scene(scene)
{
}

std::string RTReflectionsNode::name()
{
    return "rt-reflections";
}

void RTReflectionsNode::constructNode(Registry& nodeReg)
{
    std::vector<Buffer*> vertexBuffers {};
    std::vector<Buffer*> indexBuffers {};
    std::vector<RTMesh> rtMeshes {};
    std::vector<Texture*> allTextures {};

    m_scene.forEachModel([&](size_t, Model& model) {
        model.forEachMesh([&](Mesh& mesh) {
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
        });
    });

    Buffer& meshBuffer = nodeReg.createBuffer(rtMeshes, Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal);
    m_objectDataBindingSet = &nodeReg.createBindingSet({ { 0, ShaderStageRTClosestHit, &meshBuffer },
                                                         { 1, ShaderStageRTClosestHit, vertexBuffers },
                                                         { 2, ShaderStageRTClosestHit, indexBuffers },
                                                         { 3, ShaderStageRTClosestHit, allTextures, RT_MAX_TEXTURES } });
}

RenderGraphNode::ExecuteCallback RTReflectionsNode::constructFrame(Registry& reg) const
{
    Texture* gBufferColor = reg.getTexture("g-buffer", "baseColor").value();
    Texture* gBufferNormal = reg.getTexture("g-buffer", "normal").value();
    Texture* gBufferDepth = reg.getTexture("g-buffer", "depth").value();

    Texture& reflections = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA16F);
    reg.publish("reflections", reflections);

    Buffer& dirLightBuffer = reg.createBuffer(sizeof(DirectionalLightData), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);

    TopLevelAS& tlas = *reg.getTopLevelAccelerationStructure(RTAccelerationStructures::name(), "scene");
    BindingSet& frameBindingSet = reg.createBindingSet({ { 0, ShaderStage(ShaderStageRTRayGen | ShaderStageRTClosestHit), &tlas },
                                                         { 1, ShaderStageRTRayGen, &reflections, ShaderBindingType::StorageImage },
                                                         { 2, ShaderStageRTRayGen, gBufferColor, ShaderBindingType::TextureSampler },
                                                         { 3, ShaderStageRTRayGen, gBufferNormal, ShaderBindingType::TextureSampler },
                                                         { 4, ShaderStageRTRayGen, gBufferDepth, ShaderBindingType::TextureSampler },
                                                         { 5, ShaderStageRTRayGen, reg.getBuffer("scene", "camera") },
                                                         { 6, ShaderStageRTMiss, reg.getBuffer("scene", "environmentData") },
                                                         { 7, ShaderStageRTMiss, reg.getTexture("scene", "environmentMap").value_or(&reg.createPixelTexture(vec4(1.0f), true)), ShaderBindingType::TextureSampler },
                                                         { 8, ShaderStageRTClosestHit, &dirLightBuffer } });

    ShaderFile raygen = ShaderFile("rt-reflections/raygen.rgen");
    HitGroup mainHitGroup { ShaderFile("rt-reflections/closestHit.rchit") };
    std::vector<ShaderFile> missShaders { ShaderFile("rt-reflections/miss.rmiss"),
                                          ShaderFile("rt-reflections/shadow.rmiss") };
    ShaderBindingTable sbt { raygen, { mainHitGroup }, missShaders };

    uint32_t maxRecursionDepth = 2;
    RayTracingState& rtState = reg.createRayTracingState(sbt, { &frameBindingSet, m_objectDataBindingSet }, maxRecursionDepth);

    return [&](const AppState& appState, CommandList& cmdList) {

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

        cmdList.bindSet(frameBindingSet, 0);
        cmdList.bindSet(*m_objectDataBindingSet, 1);

        cmdList.traceRays(appState.windowExtent());
    };
}
