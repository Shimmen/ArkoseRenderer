#pragma once

#include "../RenderGraphNode.h"
#include "ForwardData.h"
#include "rendering/camera/FpsCamera.h"
#include "rendering/scene/Model.h"
#include "rendering/scene/Scene.h"

class SlowForwardRenderNode final : public RenderGraphNode {
public:
    explicit SlowForwardRenderNode(const Scene&);

    std::optional<std::string> displayName() const override { return "Forward"; }

    void constructNode(Registry&) override;
    ExecuteCallback constructFrame(Registry&) const override;

private:
    struct Drawable {
        const Mesh* mesh {};
        Buffer* vertexBuffer {};
        Buffer* indexBuffer {};
        uint32_t indexCount {};
        Buffer* objectDataBuffer {};
        BindingSet* bindingSet {};
    };

    std::vector<Drawable> m_drawables {};
    const Scene& m_scene;
};
