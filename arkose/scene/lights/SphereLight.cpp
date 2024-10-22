#include "SphereLight.h"

#include "asset/LevelAsset.h"
#include "core/Assert.h"
#include "rendering/debug/DebugDrawer.h"
#include "scene/lights/LightAttenuation.h"
#include <imgui.h>

SphereLight::SphereLight()
    : Light(Type::SphereLight, vec3(1.0f))
{
}

SphereLight::SphereLight(LightAsset const& asset)
    : Light(Type::SphereLight, asset)
{
    ARKOSE_ASSERT(asset.type == "SphereLight");
    ARKOSE_ASSERT(std::holds_alternative<SphereLightAssetData>(asset.data));

    auto const& data = std::get<SphereLightAssetData>(asset.data);
    m_luminousPower = data.luminousPower;
    m_lightRadius = data.lightRadius;
    m_lightSourceRadius = data.lightSourceRadius;
}

SphereLight::SphereLight(vec3 color, float luminousPower, vec3 position, float lightSourceRadius)
    : Light(Type::SphereLight, color)
    , m_luminousPower(luminousPower)
    , m_lightSourceRadius(lightSourceRadius)
{
    transform().setPositionInWorld(position);
    m_lightSourceRadius = std::max(1e-4f, lightSourceRadius);

    updateLightRadius();
}

float SphereLight::intensityValue() const
{
    // Convert lumens to candelas. Assume uniform lighting in all directions (4pi sr).
    return m_luminousPower / (4.0f * ark::PI);
}

void SphereLight::drawGui()
{
    Light::drawGui();

    ImGui::Separator();

    if (ImGui::SliderFloat("Luminous power (lm)", &m_luminousPower, 0.0f, 10'000.0f)) {
        updateLightRadius();
    }

    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        // TODO
        //DebugDrawer::get().drawSphere(transform().positionInWorld(), m_lightRadius, color());
    }

    // TODO: Make it possible to adjust radius and calculate the lumens from the radius
    //ImGui::DragFloat("Light radius", &m_lightRadius, 1.0f, 0.1f, 1'000.0f, "%.3f m", ImGuiSliderFlags_None);
    ImGui::Text("Light radius: %.2f m", m_lightRadius);

    ImGui::Separator();

    if (ImGui::SliderFloat("Light source radius", &m_lightSourceRadius, 0.01f, 1.0f, "%.3f m", ImGuiSliderFlags_None)) {
        // Ensure the light radius never becomes smaller than the light source radius
        m_lightRadius = std::max(m_lightSourceRadius + 1e-4f, m_lightRadius);
    }

    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        DebugDrawer::get().drawSphere(transform().positionInWorld(), m_lightSourceRadius);
    }
}

void SphereLight::updateLightRadius()
{
    if (m_luminousPower < 1e-4f) {
        m_lightRadius = m_lightSourceRadius + 1e-4f;
        return;
    }

    const float calibratedMaxError = 0.0375f * (8'000.0f / m_luminousPower); // calibrated for a 8k lumen lighbulb
    m_lightRadius = LightAttenuation::calculateSmallestLightRadius(m_lightSourceRadius, calibratedMaxError);
}
