#include "Light.h"

#include <format>
#include <imgui.h>

Light::Light(Type type, vec3 color)
    : m_color(color)
    , m_type(type)
{
    static int nextLightId = 0;
    m_name = std::format("light-{}", nextLightId++);
}

bool Light::shouldDrawGui() const
{
    return true;
}

void Light::drawGui()
{
    ImGui::Text("Light");
    ImGui::Separator();
    ImGui::ColorEdit3("Color", value_ptr(m_color));
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
