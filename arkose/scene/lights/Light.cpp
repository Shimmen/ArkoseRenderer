#include "Light.h"

#include <imgui.h>

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
