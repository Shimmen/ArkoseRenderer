#include "ForwardRenderNode.h"

#include "LightData.h"
#include "SceneNode.h"
#include "utility/Logging.h"

std::string ForwardRenderNode::name()
{
    return "forward";
}

ForwardRenderNode::ForwardRenderNode(Scene& scene)
    : RenderGraphNode(ForwardRenderNode::name())
    , m_scene(scene)
{
}

void ForwardRenderNode::constructNode(Registry& nodeReg)
{
    m_drawables.clear();
    m_materials.clear();
    m_textures.clear();

    m_scene.forEachMesh([&](size_t, Mesh& mesh) {
        mesh.ensureVertexBuffer(semanticVertexLayout);
        mesh.ensureIndexBuffer();

        // TODO: Remove redundant textures & materials with fancy indexing!
        //  We can't even load Sponza right now without setting up >400 texture
        //  which is obviously stupid. Texture reuse is most critical (more than
        //  material reuse, I think). Texture reuse would have to be done by the
        //  material class, as we ask the material for the texture object.

        Material& material = mesh.material();

        auto pushTexture = [&](Texture* texture) -> size_t {
            size_t textureIndex = m_textures.size();
            m_textures.push_back(texture);
            return textureIndex;
        };

        ForwardMaterial forwardMaterial {};
        forwardMaterial.baseColor = pushTexture(material.baseColorTexture());
        forwardMaterial.normalMap = pushTexture(material.normalMapTexture());
        forwardMaterial.emissive = pushTexture(material.emissiveTexture());
        forwardMaterial.metallicRoughness = pushTexture(material.metallicRoughnessTexture());

        int materialIndex = (int)m_materials.size();
        m_materials.push_back(forwardMaterial);

        m_drawables.push_back({ .mesh = mesh,
                                .materialIndex = materialIndex });
    });

    if (m_drawables.size() > FORWARD_MAX_DRAWABLES) {
        LogErrorAndExit("ForwardRenderNode: we need to up the number of max drawables that can be handled in the forward pass! We have %u, the capacity is %u.\n",
                        m_drawables.size(), FORWARD_MAX_DRAWABLES);
    }

    if (m_textures.size() > FORWARD_MAX_TEXTURES) {
        LogErrorAndExit("ForwardRenderNode: we need to up the number of max textures that can be handled in the forward pass! We have %u, the capacity is %u.\n",
                        m_textures.size(), FORWARD_MAX_TEXTURES);
    }
}

RenderGraphNode::ExecuteCallback ForwardRenderNode::constructFrame(Registry& reg) const
{
    Texture& colorTexture = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA16F);
    reg.publish("color", colorTexture);

    RenderTarget& renderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, &colorTexture },
                                                          { RenderTarget::AttachmentType::Color1, reg.getTexture("g-buffer", "normal").value() },
                                                          { RenderTarget::AttachmentType::Color2, reg.getTexture("g-buffer", "baseColor").value() },
                                                          { RenderTarget::AttachmentType::Depth, reg.getTexture("g-buffer", "depth").value() } });

    size_t perObjectBufferSize = m_drawables.size() * sizeof(PerForwardObject);
    Buffer& perObjectBuffer = reg.createBuffer(perObjectBufferSize, Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);

    size_t materialBufferSize = m_materials.size() * sizeof(ForwardMaterial);
    Buffer& materialBuffer = reg.createBuffer(materialBufferSize, Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);
    materialBuffer.updateData(m_materials.data(), materialBufferSize);

    BindingSet& objectBindingSet = reg.createBindingSet({ { 0, ShaderStage(ShaderStageVertex | ShaderStageFragment), reg.getBuffer("scene", "camera") },
                                                          { 1, ShaderStageVertex, &perObjectBuffer },
                                                          { 2, ShaderStageFragment, &materialBuffer },
                                                          { 3, ShaderStageFragment, m_textures, FORWARD_MAX_TEXTURES } });

    // TODO: Support any (reasonable) number of shadow maps & lights!
    Buffer& lightDataBuffer = reg.createBuffer(sizeof(DirectionalLightData), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);
    BindingSet& lightBindingSet = reg.createBindingSet({ { 0, ShaderStageFragment, &m_scene.sun().shadowMap(), ShaderBindingType::TextureSampler },
                                                         { 1, ShaderStageFragment, &lightDataBuffer } });

    Shader shader = Shader::createBasicRasterize("forward/forward.vert", "forward/forward.frag");
    RenderStateBuilder renderStateBuilder { renderTarget, shader, vertexLayout };
    renderStateBuilder.addBindingSet(objectBindingSet);
    renderStateBuilder.addBindingSet(lightBindingSet);
    RenderState& renderState = reg.createRenderState(renderStateBuilder);

    return [&](const AppState& appState, CommandList& cmdList) {
        // Update object data
        {
            size_t numDrawables = m_drawables.size();
            std::vector<PerForwardObject> perObjectData { numDrawables };
            for (int i = 0; i < numDrawables; ++i) {
                auto& drawable = m_drawables[i];
                perObjectData[i] = {
                    .worldFromLocal = drawable.mesh.transform().worldMatrix(),
                    .worldFromTangent = mat4(drawable.mesh.transform().worldNormalMatrix()),
                    .materialIndex = drawable.materialIndex
                };
            }
            perObjectBuffer.updateData(perObjectData.data(), numDrawables * sizeof(PerForwardObject));
        }

        // Update light data
        {
            // TODO: Upload all relevant light here, not just the default 'sun' as we do now.
            DirectionalLight& light = m_scene.sun();
            DirectionalLightData dirLightData {
                .colorAndIntensity = { light.color, light.illuminance },
                .worldSpaceDirection = vec4(normalize(light.direction), 0.0),
                .viewSpaceDirection = m_scene.camera().viewMatrix() * vec4(normalize(m_scene.sun().direction), 0.0),
                .lightProjectionFromWorld = light.viewProjection()
            };
            lightDataBuffer.updateData(&dirLightData, sizeof(DirectionalLightData));
        }

        cmdList.beginRendering(renderState, ClearColor(0, 0, 0, 0), 1.0f);
        cmdList.pushConstant(ShaderStageFragment, m_scene.ambient(), 0);

        cmdList.bindSet(objectBindingSet, 0);
        cmdList.bindSet(lightBindingSet, 1);

        m_scene.forEachMesh([&](size_t meshIndex, Mesh& mesh) {
            cmdList.drawIndexed(mesh.vertexBuffer(semanticVertexLayout),
                                mesh.indexBuffer(), mesh.indexCount(), mesh.indexType(),
                                meshIndex);
        });
    };
}
