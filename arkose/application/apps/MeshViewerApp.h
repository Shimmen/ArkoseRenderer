#pragma once

#include "application/apps/App.h"
#include "asset/MeshAsset.h"
#include "asset/import/AssetImporter.h"
#include "scene/camera/FpsCameraController.h"
#include <memory>

// TODO: Move BlendMode and ImageFilter out of the asset so we don't have to include this!
#include "asset/MaterialAsset.h"

class MeshViewerApp : public App {
public:
    void setup(Scene&, RenderPipeline&) override;
    bool update(Scene&, float elapsedTime, float deltaTime) override;

    // TODO: Probably replace with some arcball-like camera controller
    FpsCameraController m_fpsCameraController {};

private:
    Scene* m_scene { nullptr };

    // The mesh we're currently viewing & editing
    MeshAsset* m_targetAsset {};
    // The runtime version of the asset we're viewing & editing
    StaticMeshInstance* m_targetInstance {};

    int m_selectedLodIdx { 0 };
    int m_selectedSegmentIdx { 0 };

    MeshAsset& targetAsset() { return *m_targetAsset; }
    MeshLODAsset* selectedLodAsset() { return m_targetAsset ? &targetAsset().LODs[m_selectedLodIdx] : nullptr; }
    MeshSegmentAsset* selectedSegmentAsset() { return selectedLodAsset() ? &selectedLodAsset()->meshSegments[m_selectedSegmentIdx] : nullptr; }

    StaticMeshInstance& target() { return *m_targetInstance; }
    StaticMeshLOD* selectedLOD();
    StaticMeshSegment* selectedSegment();

    bool m_drawBoundingBox { false };

    void drawMenuBar();

    void drawMeshHierarchyPanel();
    
    void drawMeshMaterialPanel();
    bool drawWrapModeSelectorGui(const char* id, ImageWrapModes&);
    bool drawBlendModeSelectorGui(const char* id, BlendMode&);
    bool drawImageFilterSelectorGui(const char* id, ImageFilter&);
    
    void drawMeshPhysicsPanel();

    void importMeshWithDialog();
    void importLevelWithDialog();
    void loadMeshWithDialog();
    void saveMeshWithDialog();

    AssetImporterOptions m_importOptions { .alwaysMakeImageAsset = false,
                                           .generateMipmaps = true,
                                           .blockCompressImages = true,
                                           .saveMeshesInTextualFormat = false };

    // Since ImGui uses `const char*` for everything and we don't have a natural storage for these names we have to keep it in here...
    std::vector<std::string> m_segmentNameCache {};

};
