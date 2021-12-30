#pragma once

#include "rendering/RenderPipelineNode.h"
#include "RTData.h"
#include "rendering/scene/Model.h"
#include "rendering/scene/Scene.h"

class RTReflectionsNode final : public RenderPipelineNode {
public:
    explicit RTReflectionsNode(Scene&);
    ~RTReflectionsNode() override = default;

    std::string name() const override { return "RT Reflections"; }

    void constructNode(Registry&) override;
    ExecuteCallback constructFrame(Registry&) const override;

private:
    Scene& m_scene;
    BindingSet* m_objectDataBindingSet {};
};
