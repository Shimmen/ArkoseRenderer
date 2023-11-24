#pragma once

#include "core/Types.h"
#include "rendering/RenderPipelineNode.h"
#include <ark/vector.h>

// Contrast adaptive sharpening (https://gpuopen.com/fidelityfx-cas/)
class CASNode final : public RenderPipelineNode {
public:
    explicit CASNode(std::string textureName);

    std::string name() const override { return "Contrast Adaptive Sharpening (CAS)"; }
    void drawGui() override;

    ExecuteCallback construct(GpuScene&, Registry&) override;

    void setEnabled(bool enabled) { m_enabled = enabled; }
    void setSharpness(float sharpness) { m_sharpness = ark::clamp(sharpness, 0.0f, 1.0f); }

private:
    std::string m_textureName;

    bool m_enabled { true };
    float m_sharpness { 0.25f };
};
