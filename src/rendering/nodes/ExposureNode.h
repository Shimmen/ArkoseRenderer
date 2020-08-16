#pragma once

#include "../RenderGraphNode.h"
#include "rendering/camera/FpsCamera.h"
#include "rendering/scene/Model.h"
#include "rendering/scene/Scene.h"

class ExposureNode final : public RenderGraphNode {
public:
    explicit ExposureNode(Scene&);

    static std::string name() { return "exposure"; }
    std::optional<std::string> displayName() const override { return "Exposure / camera"; }

    void constructNode(Registry&) override;
    ExecuteCallback constructFrame(Registry&) const override;

private:
    void exposureGUI(FpsCamera&) const;
    void manualExposureGUI(FpsCamera&) const;
    void automaticExposureGUI(FpsCamera&) const;

    Scene& m_scene;
    Texture* m_lastAvgLuminanceTexture;
};
