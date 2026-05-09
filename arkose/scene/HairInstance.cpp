#include "HairInstance.h"

#include <imgui.h>

HairInstance::HairInstance(HairHandle inHair, Transform inTransform)
    : m_hair(inHair)
    , m_transform(inTransform)
{
}

HairInstance::~HairInstance() = default;

void HairInstance::drawGui()
{
    ImGui::Text("HairInstance '%s'", name.c_str());
    ImGui::Spacing();
    ImGui::Text("Transform: ");
    m_transform.drawGui();
}
