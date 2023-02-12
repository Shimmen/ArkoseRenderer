#include "SphereLight.h"

#include "core/Assert.h"
#include "rendering/debug/DebugDrawer.h"
#include "scene/lights/LightAttenuation.h"
#include <imgui.h>

SphereLight::SphereLight()
    : Light(Type::SphereLight, vec3(1.0f))
{
}

SphereLight::SphereLight(vec3 color, float inLuminousPower, vec3 position, float inLightSourceRadius)
    : Light(Type::SphereLight, color)
    , luminousPower(inLuminousPower)
    , lightSourceRadius(inLightSourceRadius)
{
    transform().setPositionInWorld(position);
    lightSourceRadius = std::max(1e-4f, lightSourceRadius);

    updateLightRadius();
}

void SphereLight::drawGui()
{
    Light::drawGui();

    ImGui::Separator();

    if (ImGui::SliderFloat("Luminous power (lm)", &luminousPower, 0.0f, 10'000.0f)) {
        updateLightRadius();
    }

    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        DebugDrawer::get().drawSphere(transform().positionInWorld(), lightRadius, color);
    }

    // TODO: Make it possible to adjust radius and calculate the lumens from the radius
    //ImGui::DragFloat("Light radius", &lightRadius, 1.0f, 0.1f, 1'000.0f, "%.3f m", ImGuiSliderFlags_None);
    ImGui::Text("Light radius: %.2f m", lightRadius);

    ImGui::Separator();

    if (ImGui::SliderFloat("Light source radius", &lightSourceRadius, 0.01f, 1.0f, "%.3f m", ImGuiSliderFlags_None)) {
        // Ensure the light radius never becomes smaller than the light source radius
        lightRadius = std::max(lightSourceRadius + 1e-4f, lightRadius);
    }

    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        DebugDrawer::get().drawSphere(transform().positionInWorld(), lightSourceRadius);
    }
}

void SphereLight::updateLightRadius()
{
    if (luminousPower < 1e-4f) {
        lightRadius = lightSourceRadius + 1e-4f;
        return;
    }

    const float calibratedMaxError = 0.0375f * (8'000.0f / luminousPower); // calibrated for a 8k lumen lighbulb
    lightRadius = LightAttenuation::calculateSmallestLightRadius(lightSourceRadius, calibratedMaxError);
}
