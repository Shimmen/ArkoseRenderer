#pragma once

#include "apps/App.h"
#include "scene/camera/FpsCameraController.h"

class MeshViewerApp : public App {
public:
    void setup(Scene&, RenderPipeline&) override;
    bool update(Scene&, float elapsedTime, float deltaTime) override;

    // TODO: Probably replace with some arcball-like camera controller
    FpsCameraController m_fpsCameraController {};

private:
    // The meshes we're currently viewing & editing
    std::vector<StaticMeshInstance*> m_targets { nullptr };

    void loadMeshWithDialog(Scene&);
    void saveMeshWithDialog(Scene&);

};
