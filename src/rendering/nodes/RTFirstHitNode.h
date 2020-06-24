#pragma once

#include "../RenderGraphNode.h"
#include "RTData.h"
#include "rendering/scene/Model.h"
#include "rendering/scene/Scene.h"

class RTFirstHitNode final : public RenderGraphNode {
public:
    explicit RTFirstHitNode(const Scene&);
    ~RTFirstHitNode() override = default;

    std::optional<std::string> displayName() const override { return "RT first-hit"; }

    static std::string name();

    void constructNode(Registry&) override;
    ExecuteCallback constructFrame(Registry&) const override;

private:
    const Scene& m_scene;
    BindingSet* m_objectDataBindingSet {};
};
