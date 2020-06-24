#include "GlobalState.h"

#include <imgui.h>

const GlobalState& GlobalState::get()
{
    static GlobalState s_globalState {};
    return s_globalState;
}

GlobalState& GlobalState::getMutable(Badge<Backend>)
{
    const GlobalState& constGlobalState = GlobalState::get();
    return const_cast<GlobalState&>(constGlobalState);
}

Extent2D GlobalState::windowExtent() const
{
    return m_windowExtent;
}
void GlobalState::updateWindowExtent(const Extent2D& newExtent)
{
    m_windowExtent = newExtent;
}

bool GlobalState::guiIsUsingTheMouse() const
{
    ImGuiIO& io = ImGui::GetIO();
    return io.WantCaptureMouse;
}
bool GlobalState::guiIsUsingTheKeyboard() const
{
    ImGuiIO& io = ImGui::GetIO();
    return io.WantCaptureKeyboard;
}
