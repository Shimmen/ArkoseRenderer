#include "SpotLight.h"

#include "asset/LevelAsset.h"
#include "rendering/backend/base/Backend.h"
#include "rendering/debug/DebugDrawer.h"
#include <imgui.h>

SpotLight::SpotLight()
    : Light(Type::SpotLight, Colors::white)
{
}

SpotLight::SpotLight(LightAsset const& asset)
    : Light(Type::SpotLight, asset)
{
    ARKOSE_ASSERT(asset.type == "SpotLight");
    ARKOSE_ASSERT(std::holds_alternative<SpotLightAssetData>(asset.data));

    auto const& data = std::get<SpotLightAssetData>(asset.data);
    m_iesProfile.load(data.iesProfilePath);
    m_luminousIntensity = data.luminousIntensity;
    m_outerConeAngle = data.outerConeAngle;
}

SpotLight::SpotLight(Color color, float luminousIntensity, const std::string& iesProfilePath, vec3 position, vec3 direction)
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

    std::string coneAngleDeg = fmt::format("{:.1f} degrees", ark::toDegrees(m_outerConeAngle));
    ImGui::SliderFloat("Max cone angle", &m_outerConeAngle, ark::toRadians(1.0f), ark::toRadians(179.0f), coneAngleDeg.c_str());

    ImGui::Separator();

    if (ImGui::TreeNode("Shadow mapping controls")) {
        ImGui::SliderFloat("Constant bias", &customConstantBias, 0.0f, 20.0f);
        ImGui::SliderFloat("Slope bias", &customSlopeBias, 0.0f, 10.0f);
        ImGui::TreePop();
    }

    DebugDrawer::get().drawArrow(transform().positionInWorld(), transform().forward(), 0.4f, color());
}

mat4 SpotLight::projectionMatrix() const
{
    return ark::perspectiveProjectionToVulkanClipSpace(m_outerConeAngle, 1.0f, m_zNear, m_zFar);
}

float SpotLight::constantBias() const
{
    return customConstantBias;
}

float SpotLight::slopeBias() const
{
    return customSlopeBias;
}
