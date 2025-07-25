#pragma once

#include "application/apps/AppBase.h"
#include "asset/MeshAsset.h"
#include "asset/import/AssetImporter.h"
#include "scene/camera/FpsCameraController.h"
#include <memory>

// TODO: Move BlendMode and ImageFilter out of the asset so we don't have to include this!
#include "asset/MaterialAsset.h"

class EditorGridRenderNode;

class MeshViewerApp : public AppBase {
public:
    void setup(Backend& graphicsBackend, PhysicsBackend* physicsBackend) override;
    bool update(float elapsedTime, float deltaTime) override;
    void render(Backend&, float elapsedTime, float deltaTime) override;

    std::vector<Backend::Capability> optionalCapabilities() override;

    // TODO: Probably replace with some arcball-like camera controller
    FpsCameraController m_fpsCameraController {};

private:
    EditorGridRenderNode* m_editorGrid { nullptr };

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
    bool drawBrdfSelectorGui(const char* id, Brdf&);
    bool drawWrapModeSelectorGui(const char* id, ImageWrapModes&);
    bool drawBlendModeSelectorGui(const char* id, BlendMode&);
    bool drawImageFilterSelectorGui(const char* id, ImageFilter&);
    
    void drawMeshPhysicsPanel();

    void importAssetWithDialog();
    void loadWithDialog();
    void saveWithDialog();

    enum class BakeMode {
        None,
        AmbientOcclusion,
        BentNormals,
    };

    BakeMode m_pendingBake { BakeMode::None };
    void drawBakeUiIfActive();

    std::unique_ptr<ImageAsset> performAmbientOcclusionBake(BakeMode, u32 resolution, u32 sampleCount);

    AssetImporterOptions m_importOptions { .alwaysMakeImageAsset = false,
                                           .generateMipmaps = true,
                                           .blockCompressImages = true,
                                           .saveMeshesInTextualFormat = false };

    std::unique_ptr<AssetImportTask> m_currentImportTask { nullptr };

    // Since ImGui uses `const char*` for everything and we don't have a natural storage for these names we have to keep it in here...
    std::vector<std::string> m_segmentNameCache {};

};
