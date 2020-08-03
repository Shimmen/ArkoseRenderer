#pragma once

#include "../RenderGraphNode.h"
#include "ForwardData.h"
#include "rendering/camera/FpsCamera.h"
#include "rendering/scene/Model.h"
#include "rendering/scene/Scene.h"

class DebugForwardNode final : public RenderGraphNode {
public:
    explicit DebugForwardNode(Scene&);

    std::optional<std::string> displayName() const override { return "Forward [DEBUG]"; }

    void constructNode(Registry&) override;
    ExecuteCallback constructFrame(Registry&) const override;

private:
    struct ForwardVertex {
        vec3 position;
        vec2 texCoord;
        vec3 normal;
        vec4 tangent;
    };

    struct Drawable {
        Mesh* mesh;
        Buffer* objectDataBuffer;
        BindingSet* bindingSet;
    };

    std::vector<Drawable> m_drawables {};
    Scene& m_scene;
};
