#include "SceneNode.h"

#include "CameraState.h"
#include "LightData.h"
#include <imgui.h>
#include <mooslib/vector.h>

std::string SceneNode::name()
{
    return "scene";
}

SceneNode::SceneNode(Scene& scene)
    : RenderGraphNode(SceneNode::name())
    , m_scene(scene)
{
}

RenderGraphNode::ExecuteCallback SceneNode::constructFrame(Registry& reg) const
{
    const DirectionalLight& light = m_scene.sun();

    Buffer& cameraUniformBuffer = reg.createBuffer(sizeof(CameraState), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);
    reg.publish("camera", cameraUniformBuffer);

    Buffer& envDataBuffer = reg.createBuffer(sizeof(float), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);
    reg.publish("environmentData", envDataBuffer);

    Texture& envTexture = m_scene.environmentMap().empty()
        ? reg.createPixelTexture(vec4(1.0f), true)
        : reg.loadTexture2D(m_scene.environmentMap(), true, false);
    reg.publish("environmentMap", envTexture);

    return [&](const AppState& appState, CommandList& cmdList) {
        ImGui::ColorEdit3("Sun color", value_ptr(m_scene.sun().color));
        ImGui::SliderFloat("Sun intensity", &m_scene.sun().illuminance, 0.0f, 50.0f);
        ImGui::SliderFloat("Environment", &m_scene.environmentMultiplier(), 0.0f, 5.0f);
        ImGui::SliderFloat("Ambient", &m_scene.ambient(), 0.0f, 1.0f);
        if (ImGui::TreeNode("Cameras")) {
            m_scene.cameraGui();
            ImGui::TreePop();
        }

        // Camera uniforms
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
        cameraUniformBuffer.updateData(&cameraState, sizeof(CameraState));

        // Environment mapping uniforms
        float envMultiplier = m_scene.environmentMultiplier();
        envDataBuffer.updateData(&envMultiplier, sizeof(envMultiplier));
    };
}
