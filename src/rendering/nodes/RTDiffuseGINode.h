#pragma once

#include "../RenderGraphNode.h"
#include "RTData.h"
#include "rendering/scene/Model.h"
#include "rendering/scene/Scene.h"

class RTDiffuseGINode final : public RenderGraphNode {
public:
    explicit RTDiffuseGINode(const Scene&);
    ~RTDiffuseGINode() override = default;

    std::optional<std::string> displayName() const override { return "Diffuse GI"; }

    static std::string name();

    void constructNode(Registry&) override;
    ExecuteCallback constructFrame(Registry&) const override;

    static constexpr int maxSamplesPerPixel = 1024;

private:
    const Scene& m_scene;

    Texture* m_accumulationTexture;
    mutable uint32_t m_numAccumulatedFrames { 0 };

    BindingSet* m_objectDataBindingSet {};
};
