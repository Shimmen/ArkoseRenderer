#pragma once

#include "../RenderGraphNode.h"
#include "ForwardData.h"
#include "rendering/camera/FpsCamera.h"
#include "rendering/scene/Model.h"
#include "rendering/scene/Scene.h"

class ForwardRenderNode final : public RenderGraphNode {
public:
    explicit ForwardRenderNode(const Scene&);

    std::optional<std::string> displayName() const override { return "Forward"; }

    static std::string name();

    void constructNode(Registry&) override;
    ExecuteCallback constructFrame(Registry&) const override;

private:
    struct Vertex {
        vec3 position;
        vec2 texCoord;
        vec3 normal;
        vec4 tangent;
    };

    struct Drawable {
        const Mesh* mesh {};
        Buffer* vertexBuffer {};
        Buffer* indexBuffer {};
        uint32_t indexCount {};
        int materialIndex {};
    };

    std::vector<Drawable> m_drawables {};
    std::vector<const Texture*> m_textures {};
    std::vector<ForwardMaterial> m_materials {};
    const Scene& m_scene;
};
