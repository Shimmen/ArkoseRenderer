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
    ImGui::ColorEdit3("Color", value_ptr(color));
}
