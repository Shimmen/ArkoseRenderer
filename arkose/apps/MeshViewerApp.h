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
    // The runtime version of the asset we're viewing & editing
    StaticMeshInstance* m_targetInstance {};

    int m_selectedLodIdx { 0 };
    int m_selectedSegmentIdx { 0 };

    StaticMeshAsset& targetAsset() { return *m_target; }
    StaticMeshLODAsset* selectedLodAsset() { return m_target ? targetAsset().lods[m_selectedLodIdx].get() : nullptr; }
    StaticMeshSegmentAsset* selectedSegmentAsset() { return selectedLodAsset() ? selectedLodAsset()->mesh_segments[m_selectedSegmentIdx].get() : nullptr; }

    StaticMeshInstance& target() { return *m_targetInstance; }
    StaticMeshLOD* selectedLOD();
    StaticMeshSegment* selectedSegment();

    void drawMenuBar();

    void drawMeshHierarchyPanel();
    
    void drawMeshMaterialPanel();
    bool drawWrapModeSelectorGui(const char* id, Arkose::Asset::WrapModes&);
    bool drawBlendModeSelectorGui(const char* id, Arkose::Asset::BlendMode&);
    bool drawImageFilterSelectorGui(const char* id, Arkose::Asset::ImageFilter&);
    
    void drawMeshPhysicsPanel();

    void openImportMeshDialog();
    void loadMeshWithDialog();
    void saveMeshWithDialog();

    // Since ImGui uses `const char*` for everything and we don't have a natural storage for these names we have to keep it in here...
    std::vector<std::string> m_segmentNameCache {};

};
