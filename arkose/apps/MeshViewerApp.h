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

    int m_selectedLodIdx { 0 };
    int m_selectedSegmentIdx { 0 };

    StaticMeshAsset& target() { return *m_target; }
    StaticMeshLODAsset* selectedLOD() { return m_target ? target().lods[m_selectedLodIdx].get() : nullptr; }
    StaticMeshSegmentAsset* selectedSegment() { return selectedLOD() ? selectedLOD()->mesh_segments[m_selectedSegmentIdx].get() : nullptr; }

    void drawMenuBar();

    void drawMeshHierarchyPanel();
    
    void drawMeshMaterialPanel();
    void drawWrapModeSelectorGui(const char* id, Arkose::Asset::WrapModes&);
    void drawBlendModeSelectorGui(const char* id, Arkose::Asset::BlendMode&);
    void drawImageFilterSelectorGui(const char* id, Arkose::Asset::ImageFilter&);
    
    void drawMeshPhysicsPanel();

    void openImportMeshDialog();
    void loadMeshWithDialog();
    void saveMeshWithDialog();

    // Since ImGui uses `const char*` for everything and we don't have a natural storage for these names we have to keep it in here...
    std::vector<std::string> m_segmentNameCache {};

};
