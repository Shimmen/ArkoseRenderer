#pragma once

#include "rendering/RenderPipelineNode.h"
#include "RTData.h"
#include "rendering/scene/Model.h"
#include "rendering/scene/Scene.h"

class RTAmbientOcclusion final : public RenderPipelineNode {
public:
    explicit RTAmbientOcclusion(const Scene&);
    ~RTAmbientOcclusion() override = default;

    std::optional<std::string> displayName() const override { return "Ambient Occlusion"; }

    static std::string name();

    void constructNode(Registry&) override;
    ExecuteCallback constructFrame(Registry&) const override;

private:
    const Scene& m_scene;

    Texture* m_accumulatedAO;
    mutable uint32_t m_numAccumulatedFrames { 0 };
};
