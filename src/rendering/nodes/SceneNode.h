#pragma once

#include "../RenderGraphNode.h"
#include "SceneData.h"
#include "rendering/scene/Model.h"
#include "rendering/scene/Scene.h"

class SceneNode final : public RenderGraphNode {
public:
    explicit SceneNode(Scene&);

    std::optional<std::string> displayName() const override { return "Scene"; }
    static std::string name();

    void constructNode(Registry&) override;
    ExecuteCallback constructFrame(Registry&) const override;

private:
    struct Drawable {
        Mesh& mesh;
        int materialIndex;
    };

    std::vector<Drawable> m_drawables {};
    std::vector<Texture*> m_textures {};
    std::vector<ShaderMaterial> m_materials {};

    Scene& m_scene;
};
