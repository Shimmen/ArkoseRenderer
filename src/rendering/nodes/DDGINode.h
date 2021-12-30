#pragma once

#include "rendering/RenderPipelineNode.h"
#include "RTData.h"
#include "rendering/scene/Model.h"
#include "rendering/scene/Scene.h"
#include "utility/Extent.h"

class DDGINode final : public RenderPipelineNode {
public:

    static constexpr int MaxNumProbeUpdatesPerFrame = 4096;
    static constexpr int RaysPerProbe = 128;

    explicit DDGINode(Scene&);
    ~DDGINode() override = default;

    std::string name() const override { return "DDGI"; }

    void constructNode(Registry&) override;
    ExecuteCallback constructFrame(Registry&) const override;

private:
    Scene& m_scene;

    Buffer* m_probeGridDataBuffer {};
    BindingSet* m_objectDataBindingSet {};
    Texture* m_probeAtlasIrradiance {};
    Texture* m_probeAtlasVisibility {};

    BindingSet* createMeshDataBindingSet(Registry&) const;
    Texture* createProbeAtlas(Registry&, const std::string& name, const ProbeGrid&, const ClearColor&, Texture::Format, int probeTileSize, int tileSidePadding) const;
};
