#include "SlowForwardRenderNode.h"

#include "ForwardRenderNode.h"
#include "LightData.h"
#include "ShadowMapNode.h"
#include <imgui.h>

SlowForwardRenderNode::SlowForwardRenderNode(Scene& scene)
    : RenderGraphNode(ForwardRenderNode::name())
    , m_scene(scene)
{
}

void SlowForwardRenderNode::constructNode(Registry& nodeReg)
{
    m_drawables.clear();
    m_scene.forEachMesh([&](size_t, Mesh& mesh) {
        Drawable drawable {};
        drawable.mesh = &mesh;

        const Material& material = mesh.material();

        // Create & load textures
        std::string baseColorPath = material.baseColor;
        Texture* baseColorTexture { nullptr };
        if (baseColorPath.empty()) {
            // the color is already in linear sRGB so we don't want to make an sRGB texture for it!
            baseColorTexture = &nodeReg.createPixelTexture(material.baseColorFactor, false);
        } else {
            baseColorTexture = &nodeReg.loadTexture2D(baseColorPath, true, true);
        }

        std::string normalMapPath = material.normalMap;
        Texture& normalMapTexture = nodeReg.loadTexture2D(normalMapPath, false, true);
        std::string metallicRoughnessPath = material.metallicRoughness;
        Texture& metallicRoughnessTexture = nodeReg.loadTexture2D(metallicRoughnessPath, false, true);
        std::string emissivePath = material.emissive;
        Texture& emissiveTexture = nodeReg.loadTexture2D(emissivePath, true, true);

        drawable.objectDataBuffer = &nodeReg.createBuffer(sizeof(PerForwardObject), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);
        drawable.bindingSet = &nodeReg.createBindingSet(
            { { 0, ShaderStageVertex, drawable.objectDataBuffer },
              { 1, ShaderStageFragment, baseColorTexture, ShaderBindingType::TextureSampler },
              { 2, ShaderStageFragment, &normalMapTexture, ShaderBindingType::TextureSampler },
              { 3, ShaderStageFragment, &metallicRoughnessTexture, ShaderBindingType::TextureSampler },
              { 4, ShaderStageFragment, &emissiveTexture, ShaderBindingType::TextureSampler } });

        m_drawables.push_back(drawable);
    });
}

RenderGraphNode::ExecuteCallback SlowForwardRenderNode::constructFrame(Registry& reg) const
{
    const RenderTarget& windowTarget = reg.windowRenderTarget();

    Texture& colorTexture = reg.createTexture2D(windowTarget.extent(), Texture::Format::RGBA16F);
    reg.publish("color", colorTexture);

    // FIXME: Make sure we can create render targets which doesn't automatically clear all input textures before writing
    RenderTarget& renderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, &colorTexture },
                                                          { RenderTarget::AttachmentType::Color1, reg.getTexture("g-buffer", "normal").value() },
                                                          { RenderTarget::AttachmentType::Color2, reg.getTexture("g-buffer", "baseColor").value() },
                                                          { RenderTarget::AttachmentType::Depth, reg.getTexture("g-buffer", "depth").value() } });

    Buffer* cameraUniformBuffer = reg.getBuffer("scene", "camera");
    BindingSet& fixedBindingSet = reg.createBindingSet({ { 0, ShaderStage(ShaderStageVertex | ShaderStageFragment), cameraUniformBuffer } });

    Texture& shadowMap = m_scene.sun().shadowMap();
    Buffer& dirLightBuffer = reg.createBuffer(sizeof(DirectionalLightData), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);
    BindingSet& dirLightBindingSet = reg.createBindingSet({ { 0, ShaderStageFragment, &shadowMap, ShaderBindingType::TextureSampler },
                                                            { 1, ShaderStageFragment, &dirLightBuffer } });

    VertexLayout vertexLayout = VertexLayout {
        sizeof(ForwardVertex),
        { { 0, VertexAttributeType::Float3, offsetof(ForwardVertex, position) },
          { 1, VertexAttributeType::Float2, offsetof(ForwardVertex, texCoord) },
          { 2, VertexAttributeType ::Float3, offsetof(ForwardVertex, normal) },
          { 3, VertexAttributeType ::Float4, offsetof(ForwardVertex, tangent) } }
    };

    Shader shader = Shader::createBasicRasterize("forward/forwardSlow.vert", "forward/forwardSlow.frag");
    RenderStateBuilder renderStateBuilder { renderTarget, shader, vertexLayout };
    renderStateBuilder.polygonMode = PolygonMode::Filled;

    renderStateBuilder.addBindingSet(fixedBindingSet);
    renderStateBuilder.addBindingSet(dirLightBindingSet);
    for (auto& drawable : m_drawables) {
        renderStateBuilder.addBindingSet(*drawable.bindingSet);
    }

    RenderState& renderState = reg.createRenderState(renderStateBuilder);

    return [&](const AppState& appState, CommandList& cmdList) {
        static bool writeColor = true;
        ImGui::Checkbox("Write color", &writeColor);
        static bool forceDiffuse = false;
        ImGui::Checkbox("Force diffuse materials", &forceDiffuse);

        // TODO: Is this even needed?? I think, yeah, in theory, but nothing is complaining.. Not a great metric, but yeah.
        m_scene.forEachMesh([](size_t, Mesh& mesh) {
            mesh.ensureIndexBuffer();
            mesh.ensureVertexBuffer({ VertexComponent::Position3F,
                                      VertexComponent::TexCoord2F,
                                      VertexComponent::Normal3F,
                                      VertexComponent::Tangent4F });
        });

        // Directional light uniforms
        // TODO: Upload all relevant light here, not just the default 'sun' as we do now.
        DirectionalLight& light = m_scene.sun();
        DirectionalLightData dirLightData {
            .colorAndIntensity = { light.color, light.illuminance },
            .worldSpaceDirection = vec4(normalize(light.direction), 0.0),
            .viewSpaceDirection = m_scene.camera().viewMatrix() * vec4(normalize(m_scene.sun().direction), 0.0),
            .lightProjectionFromWorld = light.viewProjection()
        };
        dirLightBuffer.updateData(&dirLightData, sizeof(DirectionalLightData));

        cmdList.beginRendering(renderState, ClearColor(0, 0, 0, 0), 1.0f);
        cmdList.bindSet(fixedBindingSet, 0);
        cmdList.bindSet(dirLightBindingSet, 2);

        for (const Drawable& drawable : m_drawables) {

            PerForwardObject objectData {
                .worldFromLocal = drawable.mesh->transform().worldMatrix(),
                .worldFromTangent = mat4(drawable.mesh->transform().worldNormalMatrix())
            };
            drawable.objectDataBuffer->updateData(&objectData, sizeof(PerForwardObject));

            cmdList.pushConstant(ShaderStageFragment, writeColor, 0);
            cmdList.pushConstant(ShaderStageFragment, forceDiffuse, 4);
            cmdList.pushConstant(ShaderStageFragment, m_scene.ambient(), 8);

            cmdList.bindSet(*drawable.bindingSet, 1);

            const Buffer& indexBuffer = drawable.mesh->indexBuffer();
            const Buffer& vertexBuffer = drawable.mesh->vertexBuffer({ VertexComponent::Position3F,
                                                                       VertexComponent::TexCoord2F,
                                                                       VertexComponent::Normal3F,
                                                                       VertexComponent::Tangent4F });
            cmdList.drawIndexed(vertexBuffer, indexBuffer, drawable.mesh->indexCount(), drawable.mesh->indexType());
        }
    };
}
