#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/camera/FpsCamera.h"
#include "rendering/scene/Model.h"
#include "rendering/scene/Scene.h"

class SSAONode final : public RenderPipelineNode {
public:
    explicit SSAONode(Scene&);

    std::string name() const override { return "SSAO"; }

    ExecuteCallback construct(Registry&) override;

private:
    Scene& m_scene;

    std::vector<vec4> generateKernel(int numSamples) const;
};
