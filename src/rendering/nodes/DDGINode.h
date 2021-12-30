#pragma once

#include "../RenderGraphNode.h"
#include "RTData.h"
#include "rendering/scene/Model.h"
#include "rendering/scene/Scene.h"
#include "utility/Extent.h"

class DDGINode final : public RenderGraphNode {
public:

    static constexpr int MaxNumProbeUpdatesPerFrame = 4096;
    static constexpr int RaysPerProbe = 128;

    explicit DDGINode(Scene&);
    ~DDGINode() override = default;

    std::optional<std::string> displayName() const override { return "Dynamic Diffuse GI"; }
    static std::string name();

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
