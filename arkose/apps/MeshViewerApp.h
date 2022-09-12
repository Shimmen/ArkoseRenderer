#pragma once

#include "apps/App.h"
#include "asset/StaticMeshAsset.h"
#include "scene/camera/FpsCameraController.h"
#include <memory>

class MeshViewerApp : public App {
public:
    void setup(Scene&, RenderPipeline&) override;
    bool update(Scene&, float elapsedTime, float deltaTime) override;

    // TODO: Probably replace with some arcball-like camera controller
    FpsCameraController m_fpsCameraController {};

private:
    Scene* m_scene { nullptr };

    // The mesh we're currently viewing & editing
    StaticMeshAsset* m_target {};
    StaticMeshAsset& target() { return *m_target; }

    void drawMenuBar();
    void drawMeshHierarchyPanel();
    void drawMeshMaterialPanel();
    void drawMeshPhysicsPanel();

    void openImportMeshDialog();
    void loadMeshWithDialog();
    void saveMeshWithDialog();

};
