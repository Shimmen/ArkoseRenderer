#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/camera/FpsCamera.h"
#include "rendering/scene/Model.h"
#include "rendering/scene/Scene.h"

class AutoExposureNode final : public RenderPipelineNode {
public:
    explicit AutoExposureNode(Scene&);

    static std::string name() { return "auto-exposure"; }
    std::optional<std::string> displayName() const override { return "Auto Exposure"; }

    ExecuteCallback constructFrame(Registry&) const override;

private:
    Scene& m_scene;
    mutable std::optional<BindingSet*> m_lastFrameBindingSet {};
};
