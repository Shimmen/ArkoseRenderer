#include "SlowForwardRenderNode.h"

#include "ForwardRenderNode.h"
#include "LightData.h"
#include "SceneUniformNode.h"
#include "ShadowMapNode.h"
#include <imgui.h>

SlowForwardRenderNode::SlowForwardRenderNode(const Scene& scene)
    : RenderGraphNode(ForwardRenderNode::name())
    , m_scene(scene)
{
}

void SlowForwardRenderNode::constructNode(Registry& nodeReg)
{
    m_drawables.clear();

    for (int i = 0; i < m_scene.modelCount(); ++i) {
        const Model& model = *m_scene[i];
        model.forEachMesh([&](const Mesh& mesh) {
            Drawable drawable {};
            drawable.mesh = &mesh;

            drawable.vertexBuffer = &nodeReg.createBuffer(mesh.canonoicalVertexData(), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal);
            drawable.indexBuffer = &nodeReg.createBuffer(mesh.indexData(), Buffer::Usage::Index, Buffer::MemoryHint::GpuOptimal);
            drawable.indexCount = mesh.indexCount();

            drawable.objectDataBuffer = &nodeReg.createBuffer(sizeof(PerForwardObject), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);

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

            // Create binding set
            drawable.bindingSet = &nodeReg.createBindingSet(
                { { 0, ShaderStageVertex, drawable.objectDataBuffer },
                  { 1, ShaderStageFragment, baseColorTexture },
                  { 2, ShaderStageFragment, &normalMapTexture },
                  { 3, ShaderStageFragment, &metallicRoughnessTexture },
                  { 4, ShaderStageFragment, &emissiveTexture } });

            m_drawables.push_back(drawable);
        });
    }
}

RenderGraphNode::ExecuteCallback SlowForwardRenderNode::constructFrame(Registry& reg) const
{
    const RenderTarget& windowTarget = reg.windowRenderTarget();

    Texture& colorTexture = reg.createTexture2D(windowTarget.extent(), Texture::Format::RGBA16F, Texture::Usage::AttachAndSample);
    reg.publish("color", colorTexture);

    // FIXME: Avoid const_cast and also make sure we can create render targets which doesn't automatically clear all input textures before writing
    RenderTarget& renderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, &colorTexture },
                                                          { RenderTarget::AttachmentType::Color1, const_cast<Texture*>(reg.getTexture("g-buffer", "normal").value()) },
                                                          { RenderTarget::AttachmentType::Color2, const_cast<Texture*>(reg.getTexture("g-buffer", "baseColor").value()) },
                                                          { RenderTarget::AttachmentType::Depth, const_cast<Texture*>(reg.getTexture("g-buffer", "depth").value()) } });

    const Buffer* cameraUniformBuffer = reg.getBuffer(SceneUniformNode::name(), "camera");
    BindingSet& fixedBindingSet = reg.createBindingSet({ { 0, ShaderStage(ShaderStageVertex | ShaderStageFragment), cameraUniformBuffer } });

    const Texture* shadowMap = reg.getTexture(ShadowMapNode::name(), "directional").value_or(&reg.createPixelTexture(vec4(1.0), false));
    BindingSet& dirLightBindingSet = reg.createBindingSet({ { 0, ShaderStageFragment, shadowMap },
                                                            { 1, ShaderStageFragment, reg.getBuffer(SceneUniformNode::name(), "directionalLight") } });

    Shader shader = Shader::createBasic("forward/forwardSlow.vert", "forward/forwardSlow.frag");
    RenderStateBuilder renderStateBuilder { renderTarget, shader, Mesh::canonoicalVertexLayout() };
    renderStateBuilder.polygonMode = PolygonMode::Filled;

    renderStateBuilder
        .addBindingSet(fixedBindingSet)
        .addBindingSet(dirLightBindingSet);
    for (auto& drawable : m_drawables) {
        renderStateBuilder.addBindingSet(*drawable.bindingSet);
    }

    RenderState& renderState = reg.createRenderState(renderStateBuilder);

    return [&](const AppState& appState, CommandList& cmdList) {
        static float ambientAmount = 0.0f;
        ImGui::SliderFloat("Ambient", &ambientAmount, 0.0f, 1.0f);
        static bool writeColor = true;
        ImGui::Checkbox("Write color", &writeColor);
        static bool forceDiffuse = false;
        ImGui::Checkbox("Force diffuse materials", &forceDiffuse);

        cmdList.setRenderState(renderState, ClearColor(0, 0, 0, 0), 1.0f);
        cmdList.bindSet(fixedBindingSet, 0);
        cmdList.bindSet(dirLightBindingSet, 2);

        for (const Drawable& drawable : m_drawables) {

            // TODO: Hmm, it still looks very much like it happens in line with the other commands..
            PerForwardObject objectData {
                .worldFromLocal = drawable.mesh->transform().worldMatrix(),
                .worldFromTangent = mat4(drawable.mesh->transform().worldNormalMatrix())
            };
            drawable.objectDataBuffer->updateData(&objectData, sizeof(PerForwardObject));

            cmdList.pushConstant(ShaderStageFragment, writeColor, 0);
            cmdList.pushConstant(ShaderStageFragment, forceDiffuse, 4);
            cmdList.pushConstant(ShaderStageFragment, ambientAmount, 8);

            cmdList.bindSet(*drawable.bindingSet, 1);
            cmdList.drawIndexed(*drawable.vertexBuffer, *drawable.indexBuffer, drawable.indexCount, drawable.mesh->indexType());
        }
    };
}
