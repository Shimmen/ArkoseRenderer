#include "DirectionalLight.h"

#include "asset/LevelAsset.h"
#include "rendering/debug/DebugDrawer.h"
#include <ark/quaternion.h>
#include <ark/transform.h>
#include <imgui.h>

DirectionalLight::DirectionalLight()
    : Light(Type::DirectionalLight, Colors::white)
{
}

DirectionalLight::DirectionalLight(LightAsset const& asset)
    : Light(Type::DirectionalLight, asset)
{
    ARKOSE_ASSERT(asset.type == "DirectionalLight");
    ARKOSE_ASSERT(std::holds_alternative<DirectionalLightAssetData>(asset.data));

    auto const& data = std::get<DirectionalLightAssetData>(asset.data);
    m_illuminance = data.illuminance;

    shadowMapWorldExtent = data.shadowMapWorldExtent;
}

DirectionalLight::DirectionalLight(Color color, float illuminance, vec3 direction)
    : Light(Type::DirectionalLight, color)
    , m_illuminance(illuminance)
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

    ImGui::SliderFloat("Illuminance (lux)", &m_illuminance, 0.0f, 150'000.0f);

    ImGui::Separator();

    if (ImGui::TreeNode("Shadow mapping controls")) {
        ImGui::SliderFloat("Constant bias", &customConstantBias, 0.0f, 20.0f);
        ImGui::SliderFloat("Slope bias", &customSlopeBias, 0.0f, 10.0f);
        ImGui::TreePop();
    }

    // TODO: Draw arrow!
    vec3 lightPosition = transform().positionInWorld();
    DebugDrawer::get().drawLine(lightPosition, lightPosition + forwardDirection() * 0.2f, color());
}

mat4 DirectionalLight::projectionMatrix() const
{
    return ark::orthographicProjectionToVulkanClipSpace(shadowMapWorldExtent, -0.5f * shadowMapWorldExtent, 0.5f * shadowMapWorldExtent);
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
