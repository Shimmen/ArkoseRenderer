#pragma once

#include "../RenderGraphNode.h"
#include "rendering/camera/FpsCamera.h"
#include "rendering/scene/Model.h"
#include "rendering/scene/Scene.h"

class AutoExposureNode final : public RenderGraphNode {
public:
    explicit AutoExposureNode(Scene&);

    static std::string name() { return "auto-exposure"; }
    std::optional<std::string> displayName() const override { return "Auto Exposure"; }

    ExecuteCallback constructFrame(Registry&) const override;

private:
    void exposureGUI(FpsCamera&) const;
    void manualExposureGUI(FpsCamera&) const;
    void automaticExposureGUI(FpsCamera&) const;

    Scene& m_scene;

    mutable std::optional<BindingSet*> m_lastFrameBindingSet {};
};
