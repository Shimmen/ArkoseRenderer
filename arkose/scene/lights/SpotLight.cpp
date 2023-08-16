#include "SpotLight.h"

#include "rendering/backend/base/Backend.h"
#include <imgui.h>

SpotLight::SpotLight()
    : Light(Type::SpotLight, vec3(1.0f))
{
}

SpotLight::SpotLight(vec3 color, float luminousIntensity, const std::string& iesProfilePath, vec3 position, vec3 direction)
    : Light(Type::SpotLight, color)
    , m_iesProfile(iesProfilePath)
    , m_luminousIntensity(luminousIntensity)
{
    quat orientation = ark::lookRotation(normalize(direction), ark::globalUp);
    transform().setOrientationInWorld(orientation);
    transform().setPositionInWorld(position);

    // NOTE: Feel free to adjust these on a per-light/case basis, but probably in the scene.json
    customConstantBias = 1.0f;
    customSlopeBias = 0.66f;
}

void SpotLight::drawGui()
{
    Light::drawGui();

    ImGui::Separator();

    ImGui::SliderFloat("Luminous intensity (cd)", &m_luminousIntensity, 0.0f, 1'000.0f);

    ImGui::Separator();

    if (ImGui::TreeNode("Shadow mapping controls")) {
        ImGui::SliderFloat("Constant bias", &customConstantBias, 0.0f, 20.0f);
        ImGui::SliderFloat("Slope bias", &customSlopeBias, 0.0f, 10.0f);
        ImGui::TreePop();
    }
}

mat4 SpotLight::projectionMatrix() const
{
    return ark::perspectiveProjectionToVulkanClipSpace(m_outerConeAngle, 1.0f, m_zNear, m_zFar);
}

float SpotLight::constantBias(Extent2D shadowMapSize) const
{
    int maxShadowMapDim = std::max(shadowMapSize.width(), shadowMapSize.height());
    return 0.1f * customConstantBias / float(maxShadowMapDim);
}

float SpotLight::slopeBias(Extent2D shadowMapSize) const
{
    return customSlopeBias * constantBias(shadowMapSize);
}
