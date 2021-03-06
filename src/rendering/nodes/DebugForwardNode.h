#pragma once

#include "../RenderGraphNode.h"
#include "rendering/camera/FpsCamera.h"
#include "rendering/scene/Model.h"
#include "rendering/scene/Scene.h"

class DebugForwardNode final : public RenderGraphNode {
public:
    explicit DebugForwardNode(Scene&);

    std::optional<std::string> displayName() const override { return "Forward [DEBUG]"; }

    ExecuteCallback constructFrame(Registry&) const override;

    static constexpr Texture::Multisampling multisamplingLevel() { return Texture::Multisampling::X8; }

private:
    Scene& m_scene;

    VertexLayout m_vertexLayout = VertexLayout { VertexComponent::Position3F,
                                                 VertexComponent::TexCoord2F,
                                                 VertexComponent::Normal3F,
                                                 VertexComponent::Tangent4F };
};
