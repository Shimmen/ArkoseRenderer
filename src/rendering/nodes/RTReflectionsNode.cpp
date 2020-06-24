#include "RTReflectionsNode.h"

#include "ForwardRenderNode.h"
#include "LightData.h"
#include "RTAccelerationStructures.h"
#include "SceneUniformNode.h"

RTReflectionsNode::RTReflectionsNode(const Scene& scene)
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
    std::vector<const Buffer*> vertexBuffers {};
    std::vector<const Buffer*> indexBuffers {};
    std::vector<RTMesh> rtMeshes {};
    std::vector<const Texture*> allTextures {};

    m_scene.forEachModel([&](size_t, const Model& model) {
        model.forEachMesh([&](const Mesh& mesh) {
            // TODO: Would be nice if this could be cached too!
            std::vector<RTVertex> vertices {};
            {
                auto posData = mesh.positionData();
                auto normalData = mesh.normalData();
                auto texCoordData = mesh.texcoordData();

                ASSERT(posData.size() == normalData.size());
                ASSERT(posData.size() == texCoordData.size());

                for (int i = 0; i < posData.size(); ++i) {
                    vertices.push_back({ .position = vec4(posData[i], 0.0f),
                                         .normal = vec4(mesh.transform().localNormalMatrix() * normalData[i], 0.0f),
                                         .texCoord = vec4(texCoordData[i], 0.0f, 0.0f) });
                }
            }

            const Material& material = mesh.material();
            Texture* baseColorTexture { nullptr };
            if (material.baseColor.empty()) {
                // the color is already in linear sRGB so we don't want to make an sRGB texture for it!
                baseColorTexture = &nodeReg.createPixelTexture(material.baseColorFactor, false);
            } else {
                baseColorTexture = &nodeReg.loadTexture2D(material.baseColor, true, true);
            }

            size_t texIndex = allTextures.size();
            allTextures.push_back(baseColorTexture);

            rtMeshes.push_back({ .objectId = (int)rtMeshes.size(),
                                 .baseColor = (int)texIndex });

            // TODO: Later, we probably want to have combined vertex/ssbo and index/ssbo buffers instead!
            vertexBuffers.push_back(&nodeReg.createBuffer(vertices, Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal));
            indexBuffers.push_back(&nodeReg.createBuffer(mesh.indexData(), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal));
        });
    });

    Buffer& meshBuffer = nodeReg.createBuffer(std::move(rtMeshes), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal);
    m_objectDataBindingSet = &nodeReg.createBindingSet({ { 0, ShaderStageRTClosestHit, &meshBuffer, ShaderBindingType::StorageBuffer },
                                                         { 1, ShaderStageRTClosestHit, vertexBuffers },
                                                         { 2, ShaderStageRTClosestHit, indexBuffers },
                                                         { 3, ShaderStageRTClosestHit, allTextures, RT_MAX_TEXTURES } });
}

RenderGraphNode::ExecuteCallback RTReflectionsNode::constructFrame(Registry& reg) const
{
    const Texture* gBufferColor = reg.getTexture(ForwardRenderNode::name(), "baseColor").value();
    const Texture* gBufferNormal = reg.getTexture(ForwardRenderNode::name(), "normal").value();
    const Texture* gBufferDepth = reg.getTexture(ForwardRenderNode::name(), "depth").value();

    Texture& reflections = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA16F, Texture::Usage::StorageAndSample);
    reg.publish("reflections", reflections);

    const TopLevelAS& tlas = *reg.getTopLevelAccelerationStructure(RTAccelerationStructures::name(), "scene");
    BindingSet& frameBindingSet = reg.createBindingSet({ { 0, ShaderStage(ShaderStageRTRayGen | ShaderStageRTClosestHit), &tlas },
                                                         { 1, ShaderStageRTRayGen, &reflections, ShaderBindingType::StorageImage },
                                                         { 2, ShaderStageRTRayGen, gBufferColor, ShaderBindingType::TextureSampler },
                                                         { 3, ShaderStageRTRayGen, gBufferNormal, ShaderBindingType::TextureSampler },
                                                         { 4, ShaderStageRTRayGen, gBufferDepth, ShaderBindingType::TextureSampler },
                                                         { 5, ShaderStageRTRayGen, reg.getBuffer(SceneUniformNode::name(), "camera") },
                                                         { 6, ShaderStageRTMiss, reg.getBuffer(SceneUniformNode::name(), "environmentData") },
                                                         { 7, ShaderStageRTMiss, reg.getTexture(SceneUniformNode::name(), "environmentMap").value_or(&reg.createPixelTexture(vec4(1.0f), true)) },
                                                         { 8, ShaderStageRTClosestHit, reg.getBuffer(SceneUniformNode::name(), "directionalLight") } });

    ShaderFile raygen = ShaderFile("rt-reflections/raygen.rgen");
    HitGroup mainHitGroup { ShaderFile("rt-reflections/closestHit.rchit") };
    std::vector<ShaderFile> missShaders { ShaderFile("rt-reflections/miss.rmiss"),
                                          ShaderFile("rt-reflections/shadow.rmiss") };
    ShaderBindingTable sbt { raygen, { mainHitGroup }, missShaders };

    uint32_t maxRecursionDepth = 2;
    RayTracingState& rtState = reg.createRayTracingState(sbt, { &frameBindingSet, m_objectDataBindingSet }, maxRecursionDepth);

    return [&](const AppState& appState, CommandList& cmdList) {
        cmdList.setRayTracingState(rtState);

        cmdList.bindSet(frameBindingSet, 0);
        cmdList.bindSet(*m_objectDataBindingSet, 1);

        cmdList.traceRays(appState.windowExtent());
    };
}
