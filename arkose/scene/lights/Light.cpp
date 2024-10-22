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
    m_castsShadows = asset.castsShadows;
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
    ImGui::Separator();
    ImGui::ColorEdit3("Color", m_color.asFloatPointer());
}

vec3 Light::forwardDirection() const
{
    return ark::rotateVector(transform().orientationInWorld(), ark::globalForward);
}

mat4 Light::lightViewMatrix() const
{
    vec3 position = transform().positionInWorld();
    return ark::lookAt(position, position + forwardDirection());
}
