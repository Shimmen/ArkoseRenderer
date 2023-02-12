#include "DirectionalLight.h"

#include <ark/quaternion.h>
#include <ark/transform.h>
#include <imgui.h>

DirectionalLight::DirectionalLight()
    : Light(Type::DirectionalLight, vec3(1.0f))
{
}

DirectionalLight::DirectionalLight(vec3 color, float illuminance, vec3 direction)
    : Light(Type::DirectionalLight, color)
    , illuminance(illuminance)
{
    quat orientation = ark::lookRotation(normalize(direction), ark::globalUp);
    transform().setOrientationInWorld(orientation);

    // NOTE: Feel free to adjust these on a per-light/case basis, but probably in the scene.json
    customConstantBias = 3.5f;
    customSlopeBias = 2.5f;
}

void DirectionalLight::drawGui()
{
    Light::drawGui();

    ImGui::Separator();

    ImGui::SliderFloat("Illuminance (lux)", &illuminance, 0.0f, 150'000.0f);

    ImGui::Separator();

    if (ImGui::TreeNode("Shadow mapping controls")) {
        ImGui::SliderFloat("Constant bias", &customConstantBias, 0.0f, 20.0f);
        ImGui::SliderFloat("Slope bias", &customSlopeBias, 0.0f, 10.0f);
        ImGui::TreePop();
    }
}

float DirectionalLight::constantBias(Extent2D shadowMapSize) const
{
    int maxShadowMapDim = std::max(shadowMapSize.width(), shadowMapSize.height());
    float worldTexelScale = shadowMapWorldExtent / maxShadowMapDim;

    // For the projection we use [-extent/2, +extent/2] for near & far so the full extent is the depth range
    float worldDepthRange = shadowMapWorldExtent;

    float bias = customConstantBias * worldTexelScale / worldDepthRange;
    return bias;
}

float DirectionalLight::slopeBias(Extent2D shadowMapSize) const
{
    return 0.1f * customSlopeBias * constantBias(shadowMapSize);
}
