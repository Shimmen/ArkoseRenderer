#pragma once

#include "rendering/RenderPipelineNode.h"
#include "RTData.h"
#include "rendering/scene/Model.h"
#include "rendering/scene/GpuScene.h"
#include "utility/Extent.h"

class DDGINode final : public RenderPipelineNode {
public:

    static constexpr int MaxNumProbeUpdatesPerFrame = 4096;
    static constexpr int RaysPerProbe = 128;

    std::string name() const override { return "DDGI"; }
    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    Texture& createProbeAtlas(Registry&, const std::string& name, const ProbeGrid&, const ClearColor&, Texture::Format, int probeTileSize, int tileSidePadding) const;
};
