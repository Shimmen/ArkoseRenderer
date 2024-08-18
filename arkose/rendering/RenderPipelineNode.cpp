#include "RenderPipelineNode.h"

#include "rendering/backend/base/Texture.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <utility>

const RenderPipelineNode::ExecuteCallback RenderPipelineNode::NullExecuteCallback = [](const AppState&, CommandList&, UploadBuffer&) {};

RenderPipelineLambdaNode::RenderPipelineLambdaNode(std::string name, ConstructorFunction constructorFunction)
    : m_name(std::move(name))
    , m_constructorFunction(std::move(constructorFunction))
{
}

static bool drawTextureVisualizeButton(Texture& texture, bool& isHovered)
{
    float buttonWidth = std::min(256.0f, ImGui::GetContentRegionAvail().x);
    float buttonHeight = buttonWidth / texture.extent().aspectRatio();

    bool pressed = ImGui::ImageButton(texture.asImTextureID(), ImVec2(buttonWidth, buttonHeight));
    isHovered = ImGui::IsItemHovered();

    if (!texture.name().empty()) {
        // Overlay texture name over image with some padding from top left corner
        ImGuiWindow* window = ImGui::GetCurrentContext()->CurrentWindow;
        ImGui::SameLine(window->DC.CursorPos.x + window->WindowPadding.x);
        window->DC.CursorPos.y += window->WindowPadding.y;
        ImGui::Text("%s", texture.name().c_str());
    }

    return pressed;
}

void RenderPipelineNode::drawTextureVisualizeGui(Texture& texture)
{
    ImTextureID textureId = texture.asImTextureID();

    constexpr float defaultWidth = 512.0f;
    float defaultHeight = defaultWidth / texture.extent().aspectRatio();

    bool isHovered;
    if (drawTextureVisualizeButton(texture, isHovered)) {
        m_textureVisualizers[&texture] = true;
    }

    auto entry = m_textureVisualizers.find(&texture);
    if (entry != m_textureVisualizers.end() && entry->second) {

        float windowDecorationOffset = 20.0f; // NOTE: This value could potentially be styled, meaning it's not constant.
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetMousePos().x, ImGui::GetMousePos().y - windowDecorationOffset), ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(ImVec2(defaultWidth, defaultHeight + windowDecorationOffset + 6 /* why?? */), ImGuiCond_Appearing);

        bool isOpen = entry->second;

        if (ImGui::Begin(texture.name().c_str(), &isOpen)) {
            float availableWidth = ImGui::GetContentRegionAvail().x;
            float realizedHeight = availableWidth / texture.extent().aspectRatio();
            ImGui::Image(textureId, ImVec2(availableWidth, realizedHeight));
            ImGui::End();
        }

        if (isOpen == false) {
            m_textureVisualizers[&texture] = false;
        }

    } else if (isHovered) {

        ImGui::SetNextWindowPos(ImGui::GetMousePos(), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(defaultWidth, defaultHeight), ImGuiCond_Always);

        ImGui::BeginTooltip();
        ImGui::Image(textureId, ImGui::GetContentRegionAvail());
        ImGui::End();
    }

}

RenderPipelineNode::ExecuteCallback RenderPipelineLambdaNode::construct(GpuScene& scene, Registry& reg)
{
    return m_constructorFunction(scene, reg);
}
