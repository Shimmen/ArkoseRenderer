#include "Light.h"

#include "asset/LevelAsset.h"
#include <fmt/format.h>
#include <imgui.h>

Light::Light(Type type, Color color)
    : m_type(type)
    , m_color(color)
{
    static int nextLightId = 0;
    m_name = fmt::format("light-{}", nextLightId++);
}

Light::Light(Type type, LightAsset const& asset)
    : m_type(type)
    , m_name(asset.name)
    , m_color(Color::fromNonLinearSRGB(asset.color))
    , m_transform(asset.transform)
{
    m_shadowMode = asset.castsShadows ? ShadowMode::ShadowMapped : ShadowMode::None;
    customConstantBias = asset.customConstantBias;
    customSlopeBias = asset.customSlopeBias;
}

bool Light::shouldDrawGui() const
{
    return true;
}

void Light::drawGui()
{
    ImGui::Text("Light");
    ImGui::Spacing();
    ImGui::ColorEdit3("Color", m_color.asFloatPointer());

    ImGui::Spacing();

    ImGui::Text("Shadow mode:");
    {
        ImGui::BeginDisabled(!supportsShadowMode(ShadowMode::None));
        if (ImGui::RadioButton("None", m_shadowMode == ShadowMode::None)) {
            m_shadowMode = ShadowMode::None;
        }
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    {
        ImGui::BeginDisabled(!supportsShadowMode(ShadowMode::ShadowMapped));
        if (ImGui::RadioButton("Shadow mapped", m_shadowMode == ShadowMode::ShadowMapped)) {
            m_shadowMode = ShadowMode::ShadowMapped;
        }
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    {
        ImGui::BeginDisabled(!supportsShadowMode(ShadowMode::RayTraced));
        if (ImGui::RadioButton("Ray traced", m_shadowMode == ShadowMode::RayTraced)) {
            m_shadowMode = ShadowMode::RayTraced;
        }
        ImGui::EndDisabled();
    }

    ImGui::Spacing();

    ImGui::Text("Transform:");
    m_transform.drawGui();
}

mat4 Light::lightViewMatrix() const
{
    vec3 position = transform().positionInWorld();
    return ark::lookAt(position, position + transform().forward());
}
