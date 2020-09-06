#include "SceneNode.h"

#include "CameraState.h"
#include "LightData.h"
#include "utility/Logging.h"
#include <imgui.h>
#include <mooslib/vector.h>
#include <unordered_map>

std::string SceneNode::name()
{
    return "scene";
}

SceneNode::SceneNode(Scene& scene)
    : RenderGraphNode(SceneNode::name())
    , m_scene(scene)
{
}

void SceneNode::constructNode(Registry& reg)
{
    m_drawables.clear();
    m_materials.clear();
    m_textures.clear();

    std::unordered_map<Texture*, int> textureIndices;
    auto pushTexture = [&](Texture* texture) -> int {
        auto entry = textureIndices.find(texture);
        if (entry != textureIndices.end())
            return entry->second;

        int textureIndex = static_cast<int>(m_textures.size());
        textureIndices[texture] = textureIndex;
        m_textures.push_back(texture);

        return textureIndex;
    };

    auto pushMaterial = [&](ShaderMaterial shaderMaterial) -> int {
        for (int idx = 0; idx < m_materials.size(); ++idx) {
            if (m_materials[idx] == shaderMaterial)
                return idx;
        }

        int materialIndex = static_cast<int>(m_materials.size());
        m_materials.push_back(shaderMaterial);

        return materialIndex;
    };

    m_scene.forEachMesh([&](size_t, Mesh& mesh) {
        Material& material = mesh.material();

        ShaderMaterial shaderMaterial {};
        shaderMaterial.baseColor = pushTexture(material.baseColorTexture());
        shaderMaterial.normalMap = pushTexture(material.normalMapTexture());
        shaderMaterial.emissive = pushTexture(material.emissiveTexture());
        shaderMaterial.metallicRoughness = pushTexture(material.metallicRoughnessTexture());

        int materialIndex = pushMaterial(shaderMaterial);

        m_drawables.push_back({ .mesh = mesh,
                                .materialIndex = materialIndex });
    });

    LogInfo("Scene: using %u materials & %u textures for %u drawables\n", m_materials.size(), m_textures.size(), m_drawables.size());

    if (m_drawables.size() > SCENE_MAX_DRAWABLES) {
        LogErrorAndExit("SceneNode: we need to up the number of max drawables that can be handled by the scene! We have %u, the capacity is %u.\n",
                        m_drawables.size(), SCENE_MAX_DRAWABLES);
    }

    if (m_materials.size() > SCENE_MAX_MATERIALS) {
        LogErrorAndExit("SceneNode: we need to up the number of max materials that can be handled by the scene! We have %u, the capacity is %u.\n",
                        m_materials.size(), SCENE_MAX_MATERIALS);
    }

    if (m_textures.size() > SCENE_MAX_TEXTURES) {
        LogErrorAndExit("SceneNode: we need to up the number of max textures that can be handled by the scene! We have %u, the capacity is %u.\n",
                        m_textures.size(), SCENE_MAX_TEXTURES);
    }
}

RenderGraphNode::ExecuteCallback SceneNode::constructFrame(Registry& reg) const
{
    Buffer& cameraBuffer = reg.createBuffer(sizeof(CameraState), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);
    BindingSet& cameraBindingSet = reg.createBindingSet({ { 0, ShaderStage(ShaderStageVertex | ShaderStageFragment), &cameraBuffer } });
    reg.publish("camera", cameraBuffer);
    reg.publish("cameraSet", cameraBindingSet);

    // Environment mapping stuff
    // TODO: Remove this! Barely used anyway, and not the most convenient format anyway..
    Buffer& envDataBuffer = reg.createBuffer(sizeof(float), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);
    reg.publish("environmentData", envDataBuffer);
    Texture& envTexture = m_scene.environmentMap().empty()
        ? reg.createPixelTexture(vec4(1.0f), true)
        : reg.loadTexture2D(m_scene.environmentMap(), true, false);
    reg.publish("environmentMap", envTexture);

    // Material stuff
    size_t materialBufferSize = m_materials.size() * sizeof(ShaderMaterial);
    Buffer& materialDataBuffer = reg.createBuffer(materialBufferSize, Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);
    materialDataBuffer.updateData(m_materials.data(), materialBufferSize); // TODO: Update in exec?
    reg.publish("materialData", materialDataBuffer);

    // Object data stuff
    size_t objectDataBufferSize = m_drawables.size() * sizeof(ShaderDrawable);
    Buffer& objectDataBuffer = reg.createBuffer(objectDataBufferSize, Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);
    reg.publish("objectData", objectDataBuffer);

    BindingSet& objectBindingSet = reg.createBindingSet({ { 0, ShaderStageVertex, &objectDataBuffer },
                                                          { 1, ShaderStageFragment, &materialDataBuffer },
                                                          { 2, ShaderStageFragment, m_textures, SCENE_MAX_TEXTURES } });
    reg.publish("objectSet", objectBindingSet);

    // Light data stuff
    // TODO: Support any (reasonable) number of shadow maps & lights!
    const DirectionalLight& light = m_scene.sun();
    Buffer& lightDataBuffer = reg.createBuffer(sizeof(DirectionalLightData), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);
    BindingSet& lightBindingSet = reg.createBindingSet({ { 0, ShaderStageFragment, &m_scene.sun().shadowMap(), ShaderBindingType::TextureSampler },
                                                         { 1, ShaderStageFragment, &lightDataBuffer } });
    reg.publish("lightSet", lightBindingSet);

    return [&](const AppState& appState, CommandList& cmdList) {
        ImGui::ColorEdit3("Sun color", value_ptr(m_scene.sun().color));
        ImGui::SliderFloat("Sun illuminance (lx)", &m_scene.sun().illuminance, 1.0f, 150000.0f);
        ImGui::SliderFloat("Ambient (lx)", &m_scene.ambient(), 0.0f, 1000.0f);
        if (ImGui::TreeNode("Cameras")) {
            m_scene.cameraGui();
            ImGui::TreePop();
        }

        // Update camera data
        {
            const FpsCamera& camera = m_scene.camera();
            mat4 projectionFromView = camera.projectionMatrix();
            mat4 viewFromWorld = camera.viewMatrix();
            CameraState cameraState {
                .projectionFromView = projectionFromView,
                .viewFromProjection = inverse(projectionFromView),
                .viewFromWorld = viewFromWorld,
                .worldFromView = inverse(viewFromWorld),

                .iso = camera.iso,
                .aperture = camera.aperture,
                .shutterSpeed = camera.shutterSpeed,
                .exposureCompensation = camera.exposureCompensation,
            };
            cameraBuffer.updateData(&cameraState, sizeof(CameraState));
        }

        // Update object data
        {
            size_t numDrawables = m_drawables.size();
            std::vector<ShaderDrawable> objectData { numDrawables };

            for (int i = 0; i < numDrawables; ++i) {
                auto& drawable = m_drawables[i];
                objectData[i] = {
                    .worldFromLocal = drawable.mesh.transform().worldMatrix(),
                    .worldFromTangent = mat4(drawable.mesh.transform().worldNormalMatrix()),
                    .materialIndex = drawable.materialIndex
                };
            }

            objectDataBuffer.updateData(objectData.data(), numDrawables * sizeof(ShaderDrawable));
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

        // Environment mapping uniforms
        float envMultiplier = m_scene.environmentMultiplier();
        envDataBuffer.updateData(&envMultiplier, sizeof(envMultiplier));
    };
}
