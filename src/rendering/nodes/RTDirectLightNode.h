#pragma once

#include "../RenderGraphNode.h"
#include "RTData.h"
#include "rendering/scene/Model.h"
#include "rendering/scene/Scene.h"

class RTDirectLightNode final : public RenderGraphNode {
public:
    explicit RTDirectLightNode(Scene&);
    ~RTDirectLightNode() override = default;

    std::optional<std::string> displayName() const override { return "RT direct light"; }

    static std::string name();

    void constructNode(Registry&) override;
    ExecuteCallback constructFrame(Registry&) const override;

private:
    Scene& m_scene;
    BindingSet* m_objectDataBindingSet {};
};
